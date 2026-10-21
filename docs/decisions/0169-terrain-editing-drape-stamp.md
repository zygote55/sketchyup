# Terrain editing, drape and stamp

Status: **Accepted (2026-10-19)** for the decisions below; the UX design is pending
owner approval. Roadmap [R096](../PR_ROADMAP.md#r096) (M10, scope row
[F03](../SCOPE.md)); builds on [ADR 0168](0168-terrain-import-and-surfaces.md) (R095).
R096.a (the edit core) is implemented in two stacked parts; see "Implementation notes
(R096.a)". R096.c (cavity re-triangulation and contour edit) is implemented; see
"Implementation notes (R096.c)". R096.b (sculpt and smooth) is implemented; see
"Implementation notes (R096.b)". R096.d (building-pad stamp) is implemented; see
"Implementation notes (R096.d)". R096.e (projection and drape) is implemented; see
"Implementation notes (R096.e)". Each section gives the options and trade-offs that were weighed; the owner chose the recommended option in every case (see "Decisions"). The
interaction design (§8, "Decisions the UX design must make") still needs the owner's
approval, and R096.f waits for it.

## Context

F03 asks for "contour triangulation, terrain sculpting, drape/stamp and site
coordinates with large-coordinate precision tests". R095 delivered the first half and
was accepted with open gaps on 2026-10-19 ([R095.f record](../verification/R095f-terrain.md)).
R096 is the second half.

- **Build line:** add scoped sculpt/smooth, contour edit, projection/drape and
  building-pad stamp operations with reversible previews.
- **Verify line:** contours and pad heights remain consistent; editing a local region
  preserves distant terrain and supports undo/save.
- **Gate:** R096 requires R095 and the milestone gate rule: M10 entries require M9 and
  then their own dependencies ([PR_ROADMAP](../PR_ROADMAP.md#how-to-execute-this-roadmap),
  item 3). F03 ships in its own gated minor release once R095 and R096 have both merged
  with every CI job passing ([M10 acceptance](../M10_ACCEPTANCE.md)).
- **Open R095 budget:** the R095.f budget row "one-tile replacement (R096 preview path),
  p95 ≤ 100 ms" is not applicable until R096 exists. R096 must measure it.

What R095 gives us (surveyed at `3645e221`):

- **Representation** (ADR 0168 decisions 2, 3 and 7; `src/core/terrain.hpp`,
  `src/core/terrain_records.hpp`).
  - A `BodyKind::Terrain` container carries a `TerrainRecord`: sources (contour,
    breakline, boundary, hole, points; local metres; a `reference` string but no
    stable ID), parameters, generator `cdt-2.0.0/terrain-v1`, provenance, `meshDigest`
    and `display`.
  - Its children are ordinary geometry tiles of at most 2,048 triangles, with border
    vertices duplicated in each tile.
  - The document limit is 250,000 terrain triangles, in their own limit class.
  - The mesh is stored in the binary `terrain-mesh` chunk, not re-derived on load.
  - Every mesh vertex is a source vertex, a boundary or hole vertex, or a Steiner point
    (when Steiner fill is on). CDT adds no other vertices.
- **Tiling is global.** `tileTerrainTriangles` sizes its grid cell from the whole
  mesh's centroid extent and triangle count, halves it until no cell overflows, and
  splits clusters in triangle order. A local change to the triangle set can therefore
  move every tile boundary. The grid is not stored.
- **Tile IDs are prefix-stable only.** `regenerateTerrain` keeps a tile's body ID only
  while the leading new tiles, in row-major order, equal the old ones. From the first
  difference on, every tile takes a new ID. Two rules force this:
  - a tile body's vertex and face IDs are implied (1..n, then n+1..n+m) and never go
    back, so a changed tile must be a new body;
  - `terrainMeshDigest` reads tiles in ascending ID order, and the `terrain-mesh`
    chunk lists tiles in strictly ascending body ID order
    ([ADR 0108, schema 29](0108-public-native-format.md)).

  Under this rule, a local edit near the first tile renumbers almost the whole terrain.
- **Triangulation is whole-terrain only.** `triangulateTerrain`
  (`src/geometry/terrain_triangulation.hpp`) takes a complete source set. ADR 0168 §7
  asked R095 to leave R096 "local re-triangulation through the adapter (the cavity
  API)"; that API was not built. The adapter's exact predicates (`terrainOrientation`),
  crossing checks, flat-triangle flip and determinism rules are reusable.
- **Protection.** `checkTerrainChange` (called from `Document::applyEdit`) refuses any
  change to a tile's mesh, frame or membership unless the same edit changes that
  terrain's record. R096 operations therefore always change the record, which they
  must do anyway to stay regenerable.
- **History** (D-043). Terrain edits are charged to `Document::terrainHistoryLimit`
  (512 MiB), not the 64 MiB `historyLimit`. The charge counts every changed body in
  full, including the whole `TerrainRecord` (`terrainRecordBytes`: every source
  point). With 125,000 stored points the record alone is several MB (R095.f: 6.08 MB as
  JSON). Today every record change, including a display change, charges the whole
  record.
- **Derived display is whole-terrain.** `terrainSurfaceDisplay`
  (`src/core/terrain_display.*`) recomputes normals, outer edges and the elevation
  range over all tiles whenever the tile records change. `TerrainContourCache` is
  keyed by the whole-terrain digest and regenerates every segment; 337,155 segments
  took about 0.3 s in a debug build (ADR 0168, R095.e2). `terrainElevationAt` scans
  every tile's faces for each point.
- **Previews.** Tools preview through `ToolSession::preview`
  (`src/app/tool_session.cpp`), which calls `previewBatch`
  (`src/automation/commands.cpp`):
  1. copy the Document;
  2. run the batch, including document validation of the changed bodies;
  3. return each changed body's topology as JSON (`previewGeometry`).

  `Viewport::previewCommand` draws only the **edges** of that geometry as a ghost.
  Preview never changes the revision, history or saved state. After a commit, typed
  values revise the last edit through `executeAmend` / `Document::amendLast`, as the
  Measurements box does for Push/pull.
- **Intersection.** R054's `geometry.intersect` ([ADR 0045](0045-face-intersections.md))
  is bounded to 128 target faces, 2,048 reference faces and 8,192 output edges, and
  works on planar faces. Intersect *with* terrain as a reference is allowed. Intersect
  *into* a tile is refused. There is no projection or drape feature in the product
  today.
- **Measured costs** (R095.f, Release, 250k triangles):
  - commit of a whole terrain (tiles and validation): 1.08–1.20 s, about 4–5 µs per
    triangle;
  - CDT of the whole terrain: 0.29–0.41 s;
  - complete frame on the AMD GPU: p95 0.79–0.94 ms.
- **Automation.** Terrain commands are hidden from the in-app assistant except
  `site.describe`, `terrain.describe` and `terrain.elevation_at` (ADR 0168 decision 10,
  R095.g). MCP exposes everything.

### Prior art

Research by web search on 2026-10-19. The claims below rest on the search results and
the pages they summarise; not every page was read in full. Links are under
[Sources](#sources).

- **SketchUp Sandbox** edits the mesh directly. It keeps no source of truth and no
  replayable edit list.
  - *Smoove* raises or lowers the vertices within a radius, set in the Measurements
    box, by a dragged or typed distance.
  - *Add Detail* subdivides; *Flip Edge* swaps a diagonal and is recommended for the
    flat areas left by From Contours.
  - *Stamp* flattens a pad under a grouped object and builds a transition band whose
    width is the typed *offset*. Users note that the side slopes cannot be set.
  - *Drape* projects edges vertically onto the TIN and splits its faces (an imprint).
- **Civil 3D** keeps a surface *definition*: an ordered list of operations (add
  contours, add breaklines, and edits such as add or delete point, swap edge, raise or
  lower, smooth, paste surface). Adding data updates the surface incrementally;
  removing data rebuilds it from the list.
  - *Feature lines and grading objects* project cut and fill slopes from a
    footprint to a target surface and find the daylight line.
  - Grading results feed a surface; they are not live geometry on it.
- **BricsCAD** TIN surfaces follow the same definition-list model, and the operations
  can be edited and reordered after the fact.
- **Blender sculpt** brushes have a radius, a strength and a falloff curve (presets
  include Smooth, Sphere, Linear and Constant). A "projected" falloff makes the
  influence a vertical cylinder rather than a sphere. That is what a terrain brush wants.
- **QGIS mesh editing** edits the mesh in place. "Force by polylines" makes faces follow
  lines, taking Z from the mesh or from the line, and a Delaunay operation fills faces
  over existing vertices. It records no replayable operations.
- **ArcGIS TIN** breaklines take their height "as specified" or "from surface". The
  "from surface" mode is the drape-as-imprint case below.

The surveying tools (Civil 3D, BricsCAD) answer the central question below with an
ordered operation list over source data. The modelling tools (SketchUp, Blender, QGIS)
edit the mesh and forget how they got there. SketchyUp already stores sources and
offers Regenerate, so it sits with the first group.

## 1. Scope

The four operations, in this product's terms:

| Operation | Meaning | Closest prior art |
| --- | --- | --- |
| **Sculpt / smooth** | Raise, lower or smooth the surface inside a brush (centre path, radius, falloff) or inside a closed region, changing elevations only | Smoove; Blender sculpt with projected falloff; Civil 3D raise/lower and smooth edits |
| **Contour edit** | Move, reshape, re-elevate, add or delete a source contour, breakline or spot point, with the surface re-triangulated around the change | Civil 3D add/delete line and point edits; QGIS force by polylines |
| **Projection / drape** | Project edges or face outlines vertically onto the terrain, either imprinted into the terrain or as ordinary geometry lying on it | Sandbox Drape; ArcGIS "height from surface" |
| **Building-pad stamp** | Flatten a footprint to a pad elevation and grade the surrounding terrain to it by cut and fill slopes or a fixed-width band | Sandbox Stamp; Civil 3D grading to a surface |

Every operation:

- is one labelled, undoable edit with a reversible preview (§5);
- leaves terrain outside its influence region bit-identical (§3);
- is recorded so that Regenerate reproduces it (§2).

Out of scope (§10) includes:

- Flip Edge and Add Detail;
- terrain growth beyond its outline;
- cut and fill volumes;
- non-vertical projection;
- corridors, retaining walls and live links to buildings (unless decision 4 says
  otherwise).

## 2. Edits and the terrain record

The central question is how an edit relates to the `TerrainRecord`, which today holds
the sources Regenerate rebuilds from.

### Options

**(a) Sources only.** Every edit rewrites sources, then re-triangulates locally.
Regenerate is a global CDT of the edited sources.

- *Contour edit and stamp* map naturally: change a polyline, or add the pad outline
  and daylight line as breaklines.
- *Sculpt and smooth* have no source form of their own. Because every mesh vertex is a
  source vertex, they can be expressed by changing source Z values. A contour whose
  vertices move is no longer at one elevation, so the touched runs become breaklines.
- *Regenerate:* a global CDT of the edited sources is **not** the mesh the local edits
  built. Two things differ:
  - co-circular ties resolve by insertion order, which a changed vertex set reshuffles
    everywhere (CDT's seeded shuffle);
  - the flat-triangle flip pass runs in one global queue order.

  On gridded survey points, where co-circular quads are everywhere, an edit followed
  by Regenerate can change triangles far from the edit. "Edit, then Regenerate" is not
  a no-op, and "distant terrain preserved" holds only until the next Regenerate.
- *Determinism:* each local edit is deterministic. Regenerate is too, but it equals the
  edited mesh only by accident.
- *Format:* sources gain stable IDs. No new record structure.
- *Undo and history:* every edit replaces (part of) the source list. As charged today,
  each edit costs the whole record.
- *Distant terrain:* preserved by each edit, lost at Regenerate (see above).

**(b) Mesh operations only.** Sources are frozen at creation. Every edit is a mesh
operation appended to an ordered list in the record. Regenerate is the CDT of the
sources followed by a replay of the list.

- *Sculpt and smooth* are natural.
- *Contour edit* is awkward. Re-routing a contour by a mesh operation leaves the stored
  source contour where it was, so the record's contours, the Terrain section's source
  counts and any export of sources disagree with the surface. This is exactly the
  "contours remain consistent" failure.
- *Regenerate:* by construction it reproduces the edited mesh bit for bit on one build,
  because replay runs the same deterministic code in the same order on the same input.
  When the parameters change, the list replays on the new base, and operations float to
  their locations (the Civil 3D behaviour).
- *Format:* an `edits` array in the record.
- *History:* each edit adds one small list entry plus its touched tiles.
- *Distant terrain:* preserved, including across Regenerate.

**(c) Hybrid: base sources plus an ordered edit list, with derived current sources
(recommended).**

The record keeps the imported **base sources** unchanged and gains an ordered **edit
list**. There are two kinds of entry:

- **Source edits** change the source set at their point in the list: contour edit,
  add or remove a source, the stamp's pad outline and daylight line, a drape imprint.
  Each entry stores its parameters and its **resolved geometry**: the polyline it
  inserted, with Z, so the effect on the sources is known without a mesh.
- **Surface edits** (sculpt, smooth) change elevations only. Each stores its
  parameters and its influence region (the swept brush disc or the region polygon).

The **current sources** are derived from the base sources and the list, with no mesh
and no triangulation:

- start from the base sources;
- apply each source edit's resolved geometry in order;
- clip sources from each surface edit's influence region and from each pad's daylight
  polygon (the vertices in there are now edited ground, not survey).

Regenerate is the CDT of the base sources, then the replay of the list. It reproduces
the stored mesh bit for bit on one build (a tested invariant: "Regenerate after edits is
a no-op"). Reset edits is Regenerate with the list emptied (one undoable edit).

| Consequence | (a) Sources only | (b) Mesh operations | (c) Hybrid |
| --- | --- | --- | --- |
| Regenerate after edits | Global CDT; may change distant triangles | Replay; no-op on one build | Replay; no-op on one build |
| Determinism | Per edit; Regenerate ≠ edits | Full | Full |
| Contours consistent with the record | Yes | **No**: sources go stale | Yes (current sources) |
| Format (`terrain-v1`) | Source IDs | Source IDs, `edits` | Source IDs, `edits` with resolved geometry |
| Undo | Record and touched tiles | List entry and touched tiles | List entry and touched tiles |
| History per edit (with the sharing below) | Changed sources plus tiles | One entry plus tiles | One entry plus tiles |
| Distant terrain preserved | Until Regenerate | Always | Always |
| Parameter change (Terrain settings) | Global CDT of edited sources | Replay on the new base | Replay on the new base |

**Recommendation: (c).** It is the only option that keeps both halves of the Verify
line ("contours consistent", "distant terrain preserved") true across Regenerate. It is
also the model the surveying tools use. Owner decision 1.

Rules that follow:

- **Replay outcome.** Each entry replays against the mesh state before it. An entry
  that can no longer apply, because a parameter change moved the outline from under
  its region, is **skipped with a located notice** and kept in the list, as Civil 3D
  keeps out-of-date edits. Its resolved geometry is recomputed when it does apply, for
  example a daylight line against a re-triangulated base.
- **Changing an earlier entry.** Editing or removing an entry that is not the last one
  (`terrain.update_edit`, `terrain.remove_edit`) replays from the base. It is one
  labelled edit, with a dry run on a worker, progress and cancel (the D-044 lesson). It
  is not a live-drag operation. The last entry is revised cheaply through the existing
  amend path (§5).
- **Bounds.** At most 4,096 entries and 8 MiB of encoded edit data per terrain. Both
  numbers are estimates sized for a long session of brush strokes, to be confirmed by
  R096.a. When the list is full, further edits refuse with `LIMIT_EXCEEDED`, naming
  Reset edits and Explode terrain. Folding the list into new base sources ("bake") is
  out of scope.
- **Structural sharing.** The record's base sources and edit entries become immutable
  shared parts: `shared_ptr<const …>` per source and per entry. An edit then copies
  pointers, and `terrainRecordBytes` charges only parts the previous record does not
  share. Without this, every edit would charge the whole record (several MB at 125k
  points). This fixes the existing display-edit cost too.
- **Generator identity.** It becomes `cdt-2.0.0/terrain-v1+edit-v1`. A change to any
  edit algorithm that alters replay output bumps the edit part and the pinned digests,
  as R095.b does for CDT.

## 3. Local edit mechanics

### Influence region

Each operation computes, before it changes anything:

- its **influence region**: the triangles it replaces or whose vertices it moves;
- the set of **touched tiles**: tiles holding any triangle in the influence region, or
  any triangle incident to a moved vertex.

Everything else, meaning untouched tile bodies, their IDs and every vertex outside the
region, must stay bit-identical and pointer-identical in memory. This is the testable
form of "preserves distant terrain".

### Edit mesh

Operations need adjacency across tiles (one-ring neighbours, cavities, ray walks).
R096.a adds a per-terrain **edit mesh**:

- merged vertices by exact terrain-frame position (as `TerrainSurfaceDisplay::key`
  already merges for normals);
- triangle adjacency;
- a uniform grid index over the stable tile cells below.

It is cached per terrain, keyed by the tile `BodyPtr`s, and updated from the tiles an
edit replaced rather than rebuilt. The same index serves `terrainElevationAt`, which
today scans every tile. Size estimate: about 20–30 MB at 250,000 triangles, within the
existing ≤ 200 MB added-memory budget (R095.f measured 99.8–108.6 MiB).

### Stable tiling

The tile grid becomes part of the record:

- **At creation and Regenerate,** tiling is unchanged: same cell, same buckets, same
  tiles. R095's digests and fixtures therefore stay valid. The record now stores the
  grid origin (minimum centroid X and Y) and the cell size, and each tile stores its
  **tile key**: row and column, a sub-cell path, and a part number for clusters.
- **On an edit,** each new triangle goes to the cell of its centroid in the stored
  grid; a centroid outside the original extent gets a negative row or column. A cell
  pushed over 2,048 triangles splits into quadrants (the sub-cell path), recursively,
  and a cluster no split separates is still split in triangle order.
- **A cell that falls to zero triangles** loses its tile.

Only tiles whose triangle set or vertex Z changed are rebuilt.

### Tile IDs and the digest

- A **rebuilt tile** gets a new body ID from the document allocator. Implied
  vertex and face IDs never go back, as today.
- An **untouched tile** keeps its body and ID.
- The **mesh digest** is defined over tiles in **tile-key order**, not ID order. For an
  unedited terrain the two orders coincide, so existing digests are unchanged.
- The `terrain-mesh` chunk keeps strictly ascending body ID order. It is a storage
  order, not a meaning.
- Regenerate's tile rule generalises from "leading prefix" to "a tile whose key and
  mesh are unchanged keeps its ID", so a Regenerate after edits that changes nothing
  changes no IDs.
- The digest stays one SHA-256 over all tiles. A per-commit hash of a 250k-triangle
  terrain is about 7.5 MB of input. A per-tile hash tree is an optimisation if R096.a
  measures it as a cost.

### Seams

Every tile is rebuilt from the one edit mesh, so all copies of a border vertex come
from one position and are bit-identical by construction. A moved vertex touches every
tile with a triangle incident to it. The invariant, checked in tests after every
operation:

- merged by exact position, the terrain is an edge-manifold triangle mesh;
- every edge used by one triangle lies on the outer boundary or on a hole boundary;
- all copies of a position have identical Z.

### Local re-triangulation (cavity API)

Contour edits, stamps and imprints replace a cavity. R096.c adds
`retriangulateTerrainCavity` to the adapter, with the same numbers, exact predicates,
crossing checks, report kinds and bounds as `triangulateTerrain`.

- **Input:**
  - one or more cavity boundary loops of fixed vertices with Z (constraints that must
    survive unchanged);
  - fixed interior vertices;
  - constraint segments by vertex index (existing contour and breakline segments
    recovered from the current sources);
  - the new polylines;
  - the vertices to remove.
- **Output:** triangles inside the cavity only. Boundary vertices and edges are
  untouched, so the cavity joins its surroundings exactly.
- **Rules:**
  - the flat-triangle flip runs inside the cavity only;
  - the maximum-edge-length trim does not run, since edits stay inside the outline
    (decision 6);
  - input is canonicalised by position, so the same cavity gives the same triangles.
- **Cavity choice:** the triangles crossed or touched by the removed and inserted
  geometry, plus the Bowyer–Watson cavity of each inserted vertex, closed to whole
  triangles. The result is constrained Delaunay inside the cavity, not necessarily
  globally. That is acceptable under (c), because Regenerate replays the same cavity
  operation rather than re-running a global CDT.

### Protected regions and the outline

Decided (owner decision 6):

- **Pads are protected.** Vertices on or inside a pad outline are never moved by a later
  sculpt or smooth: their falloff weight is zero. A later contour edit, stamp or
  imprint whose geometry enters a pad polygon is refused with its location. Removing
  the pad's entry lifts the protection.
- **Edits stay inside the outline.** Every edit's geometry must lie inside the terrain's
  current outline and outside its holes. Growing a terrain needs new base sources, and
  that is out of scope.

### Tiles, paint and limits

- **Paint.** A rebuilt tile keeps its tile-level material and colour. Per-face paint on
  rebuilt tiles is dropped with the `face-paint-dropped` notice that Regenerate already
  reports. Paint that must survive edits is a draped region (§4.4). Owner decision 7.
- **Limits.** Stamps and imprints add vertices. The 250,000-triangle document limit
  refuses an edit that would pass it, with a located report naming the count and the
  limit. A locked terrain, a locked ancestor or a locked tile refuses
  (`requireUnlockedTerrain`). A terrain whose frame is not level (rotated off the Z
  axis) refuses edits ("terrains are created level", R095.e2).

## 4. Operations

All inputs in commands are model coordinates in metres, as everywhere else. They are
converted once to the terrain frame and stored there, so a site re-anchor (which moves
top-level bodies, not terrain-frame data) leaves every stored edit valid.

### 4.1 Sculpt and smooth

**Inputs:**

- terrain;
- mode `raise` or `smooth`;
- scope, one of:
  - a **brush**: a stroke path of 1–256 XY points (a click is one point), a radius of
    0.01–1,000 m and a falloff `smooth`, `linear` or `constant`;
  - a **region**: one closed polygon (≤ 20,000 vertices, the adapter's polygon bound)
    with a falloff width of 0 m or more outside it.
- for `raise`: an amount in metres (negative lowers);
- for `smooth`: a strength in (0, 1] and 1–8 iterations.

**Weights.** For a vertex at plan distance *d* from the path, w = f(d / radius), with
f(0) = 1 and f(1) = 0. For `constant`, f = 1 inside and 0 at the radius and beyond.
For a region, w = 1 inside and f(d / width) outside, where d is the distance to the
polygon. The influence is a vertical cylinder (Blender's "projected" falloff), never a
sphere, so a steep slope is not under-sampled.

**Raise.** z′ = z + amount × w.

- The stroke is **swept, not accumulated**: each vertex moves once, by its distance to
  the whole path. The result does not depend on pointer sampling or dab spacing, and
  replay is exact.
- A click with a typed amount is Smoove.

**Smooth.** Jacobi iterations: every vertex in the region moves toward the plan
inverse-distance weighted mean of its one-ring neighbours,
z′ = z + strength × w × (z̄ − z). Each iteration reads only the previous iteration's
values, so the result is independent of visiting order. Neighbours outside the region
are read, never written.

**Topology.** Only Z changes. Plan triangulation is unchanged, so there is no
re-triangulation.

- The flat-triangle flip is not re-run: sculpting makes surveyed-flat triangles
  non-flat. That is intended, and the triangles involved are recorded as edited.
- **Protected vertices** are pads (decision 6). The outer boundary may move in Z.

**Contours under a sculpt.** In contour-derived terrains every vertex lies on a
contour, so sculpting moves contour vertices. Three choices:

1. Clip the touched runs out of the current sources: they become edited ground
   (recommended).
2. Skip contour vertices. On a contour terrain this makes sculpt do almost nothing.
3. Re-derive new source contours from the sculpted surface. This invents survey data
   and is costly.

Owner decision 2.

**Sparse meshes.** A brush smaller than the contour spacing may catch no vertices.
That is SketchUp's behaviour too, and densifying (Add Detail) is out of scope. The
preview highlights the vertices that will move, so an empty brush is visible.

### 4.2 Contour edit

Source edits are of four kinds, each one list entry:

- **Edit a source:** replace its points (move, insert or delete vertices; reshape),
  change a contour's elevation, or change its role between contour and breakline.
- **Add a source:** a contour, breakline or spot point, drawn or from selected edges as
  in `terrain.create_from_edges`.
- **Remove a source.**
- **Promote a display contour** to a source contour at its level. This is the way to
  edit point-only terrains, which have no source contours.

**Sources need stable IDs.** Each `TerrainSource` gains an `id` (u32, unique in the
terrain, assigned in canonical order at creation, never reused). Commands and edit
entries name sources by ID. `reference` stays the import provenance.

**Validation.** The import's rules apply, with the import's located report:

- exact-predicate crossing checks against the current sources;
- conflicting duplicates;
- zero-length and spike cleaning;
- the outline rule (decision 6) and the pad rule.

A refusal changes nothing.

**Local re-triangulation.**

- *Elevation-only change:* XY is unchanged, so it is a Z update of the contour's
  vertices plus a flat-flip pass over the incident triangles.
- *Any XY change:* a cavity (§3) around the old and new geometry is re-triangulated.
  Vertices between neighbouring contours are re-interpolated by the new triangles, so
  the surface follows the moved contour.

**Preview.** Dragging a contour vertex re-runs the cavity on a worker, latest wins (the
R095.d pattern). A cavity is typically hundreds of triangles. R095.b measured the whole
adapter at 9–14 ms for 10k triangles, so this fits the 100 ms preview budget by a wide
margin (estimate).

### 4.3 Building-pad stamp

**Inputs:**

- terrain;
- footprint, one of:
  - a selected horizontal face;
  - the lowest horizontal face of a selected group or component;
  - a closed edge loop;
  - an explicit polygon.

  The footprint is projected to plan. It must be simple, with at most 20,000 vertices
  and no holes.
- pad elevation: world Z; default the footprint's own Z. A pad offset below the
  footprint (for slab build-up) is a UX choice.
- grading mode (owner decision 3):
  - **Daylight** (recommended default; Civil 3D): a cut slope and a fill slope as
    rise/run (default 1:2, i.e. 0.5) and a maximum daylight distance (default 100 m;
    estimate).
  - **Fixed width** (SketchUp's offset): a band width in metres; the slope is whatever
    joins the pad edge to the ground at that distance.

**Daylight solver.** Daylight runs outward from the pad outline in plan:

- from each outline vertex along its outward normal;
- along each edge at a spacing no larger than the local triangle size;
- with a fan of rays at no more than 15° spacing around each convex corner;
- with one bisector ray at each concave corner.

Along each ray the grade is z = h − s_fill·t while it is above the ground and
z = h + s_cut·t while it is below. The solver walks the edit mesh to the first
crossing with the ground surface, which is piecewise linear along the ray, so the
crossing is exact per triangle. The crossings, in order, form the **daylight line**.

- **Self-intersection.** A daylight polygon that crosses itself (tight re-entrant
  corners) keeps its outer boundary and reports the repair.
- **Refusals.** A ray that leaves the terrain, or reaches the maximum distance without
  daylighting, refuses with the ray's location.
- **Mixed cut and fill.** Where the pad edge meets the ground exactly (t = 0), the
  daylight touches the pad. Mixed cut and fill along one outline is normal.

**Mesh result.** One cavity replaces everything inside the daylight polygon:

- the pad interior is a flat CDT of the outline at exactly *h*;
- the pad outline and the daylight line are constraints;
- every ray is a constraint segment from its pad vertex to its daylight point, so the
  side slope is exact along each ray and no triangle spans the pad diagonally;
- current sources inside the daylight polygon are clipped (counted in the report).

Pad triangles are flat by design and are excluded from the flat-triangle report.

**Linkage.** The stamp is **one-shot**. The entry copies the footprint polygon and
remembers the source body ID only for a "Restamp" action. Restamp re-reads the
footprint and replaces the entry's parameters: one edit, with a replay when the entry
is not the last. Moving the building does not move the pad, as in SketchUp and as with
Civil 3D grading on a static footprint. A live link would need the dependency tracking
and "Changed" state of [ADR 0166](0166-configurable-component-interface.md) across
bodies. Owner decision 4.

**Revision.** Right after placement, typing a new elevation or slope in the
Measurements box amends the stamp through `ToolSession::commit` (amend), as Push/pull
takes a typed distance. This re-runs the stamp against the pre-stamp state and needs no
replay.

**Overlaps.** A stamp whose daylight polygon enters another pad's outline is refused
(decision 6). Entering only another pad's slopes is allowed: the newer slope wins
there, and the older pad's slope check (§6, check C3) then covers only its unsuperseded
rays.

### 4.4 Projection and drape

**Inputs:**

- terrain;
- edges and curves `{body, edge}` and faces `{body, face}` in any context, read in
  world coordinates as `terrain.create_from_edges` does;
- mode `imprint` or `overlay`;
- for imprint, an optional `paintRegions` flag.

Projection is vertical in the terrain frame. Parts outside the outline are clipped and
counted.

**Splitting.** Each input segment is projected in plan and split at every crossing
with a mesh edge and at every mesh vertex it passes within 1e-7 m of (snapped to that
vertex). Each piece then lies inside one triangle. Its Z comes from that triangle's
plane, by barycentric interpolation at the split points. The draped polyline therefore
lies exactly on the existing surface. A crossing with an existing contour takes the
contour's exact Z.

**Imprint** (recommended default; SketchUp's Drape):

- The split polyline is inserted by **splitting the crossed triangles only**. Each
  crossed triangle is cut along the pieces and re-triangulated within itself. There is
  no Delaunay re-triangulation.
- Every new triangle is coplanar with the triangle it came from, so **the surface shape
  does not change**. Testably, `terrain.elevation_at` at any XY is unchanged within
  1e-9 m.
- Imprinted lines join the current sources as breaklines with surface Z (ArcGIS
  "height from surface").
- A face's loops imprint as closed breaklines. With `paintRegions`, the entry records
  the loop and the face's material, and every triangle whose centroid lies inside the
  loop takes that material, on each replay as well. This gives paths and driveways
  paint that survives later edits and Regenerate, which per-face paint on tiles does
  not (decision 7).
- Slivers below the document face tolerance (twice the area under 1e-12 m²) are avoided
  by the 1e-7 m vertex snap. If one would still occur, the imprint refuses with its
  location.

**Overlay:**

- Overlay creates ordinary geometry in a new group in the active context: the split
  polyline as edges, and draped faces as one planar polygon per triangle piece (a
  draped face is not planar as a whole).
- The terrain and its record do not change.
- Overlay counts against the modelling limits (100,000 faces), not the terrain limit.
- Coplanar overlay faces would z-fight with the terrain. Overlay lifts them by a small
  offset (default 0.01 m; estimate) or relies on the renderer's polygon offset; this is
  a UX choice.
- The draped group is not linked to its source edges.

Owner decision 5: both modes, with imprint the default. Overlay suits property lines
and setbacks that must stay ordinary geometry. Imprint suits anything to be painted or
graded later.

**Intersect.** R054's `geometry.intersect` stays as it is. Drape does not use it. Its
planar-face kernel and its 2,048-reference-face bound fit one tile, not a draped line
crossing many, and the split above is simpler and exact on a TIN.

## 5. Reversible previews

**Contract.** The `ToolSession` contract holds: a preview never changes the revision,
history, dirty state or encoding; Escape discards it; commit runs the same command
through the batch API as one labelled edit.

**Why not the generic preview.** `previewBatch` is the wrong path for terrain, for
three reasons:

1. It validates every changed tile body in a document copy.
2. It serialises their topology to JSON.
3. `Viewport::previewCommand` draws the result as an **edge ghost**. That would draw
   every interior edge of the touched tiles, which R095.e2 hides.

**Terrain preview path:**

1. The tool calls the operation's core function on the **edit mesh**, not on a document
   copy. The result is the candidate triangles and vertex Z per touched tile key.
   Nothing is validated or committed.
2. The viewport's terrain renderer draws the candidate tiles **in place of** the
   originals: it substitutes their vertex buffers and recomputes normals for the
   touched tiles and the seam ring around them. The candidate is drawn as surface,
   with the changed region outlined. Stamps also tint cut and fill (UX).
3. Contour lines for the touched tiles are recomputed per tile (R096.a makes contours
   and surface display per tile; see Context).
4. Cancel restores the original buffers. Nothing else exists to revert.
5. Commit runs the command. A test checks that the committed tile meshes equal the
   previewed candidate bit for bit.

Sculpt is cheap enough to compute on the UI thread. Contour edit, stamp and drape run
on a one-thread worker, latest wins, as in R095.d.

**The R095 budget, made precise.** "One-tile replacement, p95 ≤ 100 ms" is measured
from the input event to the presented frame that shows the updated preview, for an
operation whose touched tiles hold at most 2,048 triangles in all, on a
250,000-triangle terrain. It is the BUILD_PLAN routine-edit budget ("p95 feedback below
100 ms"). Sculpt with a radius that touches nine tiles is measured and reported
alongside.

**Commit.** Commit of a one-tile edit gets the same budget (proposed): p95 ≤ 100 ms
from release to the frame showing the committed state. The R095.f commit rate (about
4–5 µs per triangle) puts a full 2,048-triangle tile near 10 ms (estimate by
proportion), plus the digest and the incremental display update. Operations that
replay the list (editing an earlier entry, Regenerate, Terrain settings) show progress
and cancel, and are budgeted in §9.

## 6. Consistency

"Contours and pad heights remain consistent" means the following checks. Each must
hold after every operation, after Undo and Redo, after save and reopen, and after
Regenerate.

| ID | Check |
| --- | --- |
| C1 | Every **current** source contour and breakline lies on the surface. Each vertex is a mesh vertex with Z exactly its stored value (bit-equal; contours at their one elevation), and each segment is a mesh edge. C1 covers the base sources and the entries that applied: an entry a replay skipped (its `edit-skipped` notice names it) is left out, since the mesh does not hold its change (amended with R096.c). |
| C2 | Display contour lines at a current source contour's elevation pass through that contour within 1e-9 m, and display contours reflect the edited surface with no stale tiles (per-tile cache keys). |
| C3 | For every pad not removed, every mesh vertex on or inside its outline has Z bit-equal to the pad elevation. `terrain.elevation_at` inside it returns the pad elevation within 1e-9 m. Its outline edges are mesh edges. Along every unsuperseded ray, the grade |z_pad − z_daylight| / run equals the fill slope where the pad is above the ground and the cut slope where it is below, within 1e-9 relative, and the daylight point lies on the surface within 1e-9 m. |
| C4 | Imprint changes no elevation: `terrain.elevation_at` at 10,000 random points in the region is unchanged within 1e-9 m. |
| C5 | Outside the influence region, every tile body is pointer-identical with the same ID, and every vertex is bit-identical. |
| C6 | Seams: the edge-manifold and identical-copies invariant of §3. |
| C7 | Regenerate after any sequence of edits is a no-op (identical record, IDs and digest). |

## 7. Persistence

Schema 29 and `terrain-v1` have not shipped in a release; 1.2.0 predates them. The
recommendation is to **amend `terrain-v1` in place**, as the R095.e2 display addendum
did, rather than add schema 30 or a second feature (owner decision 8). The amendment
adds:

- **`id`** on each source (required; u32, unique within the terrain).
- **`tiling`** on the record: `{origin: [x, y], cell}`. Each tile marker gains its
  **key**: `terrainTile` becomes an object `{key: [row, column, path, part]}` in place
  of `true`.
- **`edits`** on the record: an ordered array, omitted when empty. Every entry has an
  `id` and a `kind` (`sculpt`, `smooth`, `editSource`, `addSource`, `removeSource`,
  `stamp`, `drape`) with its parameters, its influence region or resolved geometry in
  the terrain frame, and, for a stamp, its optional `sourceBody`. Bounds:
  - per entry: 256 path points, 20,000 polygon vertices;
  - per list: 4,096 entries and 8 MiB (§2).
- **Generator** `cdt-2.0.0/terrain-v1+edit-v1`.
- **Digest** in tile-key order (§3).

Consequences:

- The `terrain-mesh` chunk layout is unchanged.
- The decoder checks every bound before allocating.
- Development documents written by R095 builds still load: a missing `id`, `tiling` or
  key is derived on load, in canonical source order and the creation tiling, and the
  file is rewritten in the new form on save. That costs one loader branch, removable
  before the F03 release.
- The fixtures, `docs/api/native-format-v1.json`, the ADR 0108 addendum and the fuzz
  seeds (`parser_fuzz_terrain_mesh`, plus a new `parser_fuzz_terrain_edits` for the
  edit list decoder) are regenerated or extended.

## 8. Interaction surface

### Commands

Commands follow the `noun.verb` catalogue. Each is one labelled edit with the shared
dry run (`previewBatch`), revision checks and error codes, and returns the bounded
terrain report (`terrainOperations`, as R095.c2).

| Command | Purpose | History label |
| --- | --- | --- |
| `terrain.sculpt` | `{terrain, mode: raise\|smooth, path?, radius?, falloff?, region?, falloffWidth?, amount?, strength?, iterations?}` | "Sculpt terrain" / "Smooth terrain" |
| `terrain.edit_source` | `{terrain, source, points?, elevation?, role?}` | "Edit contour" |
| `terrain.add_source` | `{terrain, role, points \| edges, elevation?}`, including promotion of a display contour (`level`) | "Add contour" |
| `terrain.remove_source` | `{terrain, source}` | "Remove contour" |
| `terrain.stamp_pad` | `{terrain, footprint: {body, face} \| {body} \| {edges} \| {polygon}, elevation?, mode: daylight\|width, cutSlope?, fillSlope?, maxDistance?, width?}` | "Stamp pad" |
| `terrain.drape` | `{terrain, edges?, faces?, mode: imprint\|overlay, paintRegions?, lift?, context?}`; `context` (R096.e): the group an overlay goes in | "Drape on terrain" |
| `terrain.update_edit` | `{terrain, edit, …parameters}`: change a recorded entry (Restamp; pad elevation or slopes); replays from that entry | "Edit terrain change" |
| `terrain.remove_edit` | `{terrain, edit}`; replays | "Remove terrain change" |
| `terrain.reset_edits` | Empty the list; Regenerate from base sources | "Reset terrain edits" |

`terrain.describe` adds:

- `edits`: count, bytes, and the entries with their kind, ID and bounding box (at most
  200 listed; the rest counted);
- `currentSources`: counts by role;
- `pads`: elevation and outline extent.

`terrain.regenerate` replays the list.

**MCP and the assistant.** MCP exposes everything. Following ADR 0168 decision 10, the
in-app assistant gets **no new tools**. Its `terrain.describe` description gains one
clause about `edits`, and `assistant_tool_surface_tests` stays pinned. The reasons:
edits are spatial, the assistant has no way to show a terrain preview before it acts,
and every visible tool costs tokens on every turn (R095.g measured about 1,000 for
three queries). Exposing `terrain.stamp_pad` later, after a corpus measurement, is the
obvious first candidate. Owner decision 9.

### Desktop entry points

- A **Terrain tools** group: Sculpt, Smooth, Edit contours, Stamp pad and Drape, as
  tools on the rail and in a Terrain menu (placement is a UX decision).
- An **Edits** list in the Entity info Terrain section, with Remove, Edit pad…, Reset
  edits and the notices of the last replay.
- Each tool refuses outside a terrain with the existing hover pattern of R095.e1.

### Decisions the UX design must make

The owner approves a UX design separately, as with R095.

1. Where the terrain tools live (rail group, Terrain menu, context menu on a terrain),
   their names and any default shortcuts (conflicts checked).
2. The brush:
   - how radius, falloff, amount, strength and iterations are set (Measurements box as
     in Smoove, on-canvas drag, modifier keys);
   - how the brush is drawn on the surface;
   - stroke versus click;
   - how vertices that will move are highlighted.
3. Region scope: drawing a region polygon or using a selected loop, and how the falloff
   width is shown.
4. Contour editing:
   - showing source contours distinctly from display contours;
   - vertex and segment handles;
   - typing an elevation;
   - adding, deleting and promoting contours;
   - showing refusals (crossings, outline, pads) at the pointer.
5. Stamp:
   - picking the footprint;
   - pad elevation by drag and by typing;
   - the pad offset below the footprint;
   - the grading mode and slopes;
   - cut and fill tint in the preview;
   - the daylight line;
   - Restamp.
6. Drape: choosing imprint or overlay, region paint, the overlay lift, and what is
   selected afterwards.
7. The preview's look: candidate surface, changed-region outline, and the colours for
   cut, fill and refusal.
8. The Edits list:
   - what each row shows;
   - selecting a row to highlight its region;
   - Remove, Edit pad… and Reset edits, with their confirmations and replay progress;
   - skipped-entry notices.
9. Notices: paint dropped on rebuilt tiles, sources clipped, entries skipped on replay,
   and the report tree reuse.
10. Keyboard and accessibility for brush tools: a keyboard-only path (Measurements box
    entry at a typed position, using `@` site coordinates from R095.e2), accessible
    names, and 1×/2×, fractional and 200 % text.

## 9. Verification plan

Fixtures (in-repo, generated, no third-party data):

- the R095 `terrain-contours-m.dxf` (hill, hairpin valley, boundary, pond);
- the two 250k terrains of `terrain_budget_benchmark` (points and contours, 256 and
  3,169 tiles);
- a **gridded** spot-point terrain (regular 1 m grid, co-circular everywhere), for the
  distant-terrain and Regenerate checks;
- an L-shaped footprint on a slope, giving mixed cut and fill and a concave corner;
- a footprint over a tile corner (four tiles);
- a draped path crossing several contours and tile seams.

| Verify clause | Check |
| --- | --- |
| Contours remain consistent | C1 and C2 after each operation kind and after a scripted sequence of all of them, on the contour fixture |
| Pad heights remain consistent | C3 on the L-shape and tile-corner pads, after later sculpts near the pad (protected vertices unchanged), a refused contour edit into the pad (nothing changed) and a Restamp |
| Local edit preserves distant terrain | C5 on the gridded and the 250k fixtures: untouched tiles pointer-identical with their IDs, vertices outside the region bit-identical, `terrain.elevation_at` at 10,000 points outside the region bit-equal |
| Undo | Each operation: Undo restores the record and tiles pointer-identical and the encoding bit for bit; Redo restores the edit; after a sequence of N edits, N Undos give the original encoding |
| Save | Save and reopen bit for bit, including the edit list, keys and tiling; C7 (Regenerate is a no-op) after reopen |
| Reversible previews | A preview leaves revision, history, dirty state and encoding unchanged; Escape restores the frame; committed meshes equal the previewed candidate |
| Seams | C6 after every operation, on the tile-corner fixture and the 250k contours terrain (79-triangle tiles, many seams) |
| Imprint keeps shape | C4 |
| Determinism | The same operation on two copies gives identical records and IDs; replay digests pinned per fixture |
| Limits and refusals | Triangle limit by stamp and imprint; locked terrain; outside the outline; into a pad; crossing contour edit (located, bounded report); full edit list; history-budget refusal. Each leaves revision, history and records unchanged |
| Cavity adapter | Unit tests and a libFuzzer target `parser_fuzz_terrain_cavity` (random cavities with constraints) run twice per input for determinism; the edit-list decoder fuzz target |

Budgets, measured by a `terrain_edit_benchmark` next to `terrain_budget_benchmark`,
under the R095.f conditions (pinned image, software rendering, plus the AMD session
for frames). Rows marked *proposed* were adopted by the owner (decision 10) and are
confirmed by R096.a's measurement before they are frozen.

| Operation (250k-triangle terrain) | Budget |
| --- | --- |
| One-tile replacement: input to preview frame (R095.f row) | p95 ≤ 100 ms |
| Sculpt touching nine tiles: input to preview frame | Measured and reported |
| One-tile edit: release to committed frame | p95 ≤ 100 ms (*proposed*) |
| Contour edit cavity of ≤ 2,048 triangles: preview | p95 ≤ 100 ms (*proposed*) |
| Stamp of a 64-vertex footprint: preview | Measured and reported |
| Regenerate, Terrain settings, update or remove an entry, replaying 1,000 entries | ≤ 3 s, with progress and cancel (*proposed*; reuses the parse-and-triangulate row) |
| Undo depth | At least 64 consecutive one-tile edits stay undoable within the 512 MiB terrain history (*proposed*; ~1 MB per full tile, ADR 0168 §2) |
| Added resident memory with the edit mesh cache | Within the existing ≤ 200 MB row |
| Complete frame with the terrain and a preview visible | Within the M9 frame budget |

Native tests follow the established matrix: a `terrain_edit_input_tests` suite on X11
and Wayland at 1× and 2×, with accessibility, keyboard-reachability and text-size
extensions for the tools and the Edits list.

## 10. Out of scope

For R096:

- Flip Edge and Add Detail (densify) as user operations. Both fit the edit list later.
- Growing a terrain beyond its outline, and adding base sources after creation.
- Baking the edit list into new base sources.
- Cut and fill volumes. They were also out of scope in ADR 0168. The stamp preview
  tints cut and fill areas but reports no volumes.
- Non-vertical projection, and drape onto ordinary (non-terrain) geometry.
- Pads with holes, merged pads and pads with different slopes per edge. One cut slope
  and one fill slope per pad.
- Corridors, retaining walls and feature lines along roads.
- Live links from pads to buildings (unless decision 4 chooses them).
- Carrying per-face tile paint across a rebuild (decision 7).
- Steiner refinement and quality meshing of edited regions.

## 11. Risks

- **Replay fidelity.** Bit-identical replay depends on every operation being a pure
  function of the mesh, its parameters and its order. Hidden state (hash-map iteration
  order, threading, uninitialised scratch) would break C7 silently. Mitigations:
  - pinned replay digests per fixture;
  - the fuzz harness runs every input twice;
  - no parallelism inside operations.
- **Cavity robustness.** A new constrained-triangulation entry point inside a fixed
  boundary is the hardest new code. It reuses the adapter's exact predicates and is
  fuzzed, but CDT's behaviour on a cavity whose boundary is nearly collinear needs the
  same scrutiny R095.b gave whole terrains.
- **Daylight geometry.** Concave footprints and steep or irregular ground produce
  self-intersecting daylight polygons. The repair (keep the outer boundary) can
  surprise users. The preview must show it, and the report names it.
- **Display incrementality.** Normals, contour lines and elevation bands are
  whole-terrain today. If R096.a does not make them per tile, the 100 ms budgets fail
  at 250k regardless of how fast the operations are.
- **Undo depth.** A wide brush on 2,048-triangle tiles touches several MB per stroke. The
  512 MiB budget then holds tens, not thousands, of strokes (estimate). The proposed
  64-edit floor needs measuring.
- **Sparse contour meshes.** Sculpt moves only existing vertices. On contour terrains
  with long thin triangles, results can look faceted until Add Detail exists.

## Implementation split

Each slice is an independently testable review unit. The UX design must be approved
before R096.f.

- **R096.a — Edit core.** Requires R095. Delivers:
  - the `terrain-v1` amendments: source IDs, tiling, tile keys, the `edits` list, the
    generator and the key-order digest, with the ADR 0108 addendum, API schema,
    fixtures and fuzz seeds;
  - structural sharing in `TerrainRecord` and the history charge;
  - the edit mesh with adjacency and its grid index, also used by `terrainElevationAt`;
  - stable tiling and local tile replacement (`replaceTerrainRegion`);
  - the generalised tile-ID rule;
  - the seam invariant checker;
  - per-tile surface display and contour caches;
  - the replay engine, with `terrain.reset_edits`, `terrain.remove_edit` and replay in
    `terrain.regenerate` and `terrain.set_parameters`;
  - progress and cancel for replay (which also gives D-044 the cancellation plumbing it
    lacks);
  - `terrain_edit_benchmark` with a test-only operation, so the one-tile budget is
    measurable first.
- **R096.b — Sculpt and smooth.** Requires R096.a. `terrain.sculpt` (brush and region,
  swept raise, Jacobi smooth), pad protection, and contour clipping per decision 2.
- **R096.c — Cavity re-triangulation and contour edit.** Requires R096.a.
  `retriangulateTerrainCavity` in the adapter with its fuzz target, and
  `terrain.edit_source`, `terrain.add_source` (with promotion) and
  `terrain.remove_source`.
- **R096.d — Pad stamp.** Requires R096.c. The daylight and fixed-width solvers,
  `terrain.stamp_pad`, `terrain.update_edit` (Restamp and pad parameters), and the
  overlap and protection rules.
- **R096.e — Drape.** Requires R096.a; uses R096.c's report kinds. Split-only imprint,
  overlay, region paint, and `terrain.drape`.
- **R096.f — Desktop tools.** Requires R096.b–e and the approved UX design. The terrain
  preview path in the viewport, the five tools, the Measurements box and amend, the
  Edits list, and `terrain_edit_input_tests` with the accessibility, keyboard and
  text-size extensions. It may split into f1 (preview path, Sculpt and Smooth) and f2
  (contour, stamp and drape tools, Edits list).
- **R096.g — Acceptance record.** `docs/verification/R096g-terrain-editing.md` with a
  JSON summary. It covers:
  - the Verify line and every check of §9;
  - the budgets, including R095.f's one-tile replacement row, which it closes;
  - the native matrix on X11 and Wayland at 1× and 2×;
  - sanitizers and fuzzing.

## Decisions

The owner decided these points on 2026-10-19, each as recommended:

1. **Edit model:** hybrid (c): the record keeps the base sources and an ordered edit list whose entries store their parameters and resolved geometry; current sources are derived; Regenerate is the CDT of the base sources plus a replay of the list, and is a no-op after edits.
2. **Sculpt over source contours:** contour vertices a sculpt or smooth moves are clipped out of the current sources and become edited ground.
3. **Pad grading:** daylight by separate cut and fill slopes to the surface (default 1:2, maximum distance 100 m) is the default mode; SketchUp's fixed-width band is the second mode.
4. **Stamp linkage:** one-shot; the entry copies the footprint and offers Restamp; there is no live link to the building.
5. **Drape result:** imprint into the terrain by splitting the crossed triangles only (shape unchanged, optional persistent region paint) is the default; overlay as ordinary geometry in a group is the second mode.
6. **Protected regions and outline:** sculpt and smooth never move pad vertices; contour edits, stamps and imprints that enter a pad or leave the terrain's outline refuse with a location.
7. **Paint on edited tiles:** rebuilt tiles keep their tile-level material; per-face paint on them is dropped with a notice; paint that must survive edits is a draped region.
8. **Format:** the unshipped `terrain-v1` is amended in place (source IDs, tiling and tile keys, `edits`, key-order digest); there is no schema 30.
9. **Assistant visibility:** no new assistant tools; MCP exposes every R096 command; `terrain.describe` reports edits.
10. **New budgets:** one-tile commit p95 ≤ 100 ms; contour-edit preview p95 ≤ 100 ms for cavities up to 2,048 triangles; a replaying Regenerate ≤ 3 s at 250k with progress and cancel; at least 64 undoable one-tile edits. R096.a's measurement confirms them before they are frozen.

**Interaction design.** Approved by the owner on 2026-10-20: [UX_DESIGN section 15](../UX_DESIGN.md#15-terrain-editing)
answers §8's ten questions, with the mockups in `docs/mockups/terrain.html`. R096.f
builds it.

## Owner decisions

1. **Edit model:** hybrid (c): base sources plus an ordered edit list with resolved geometry, current sources derived, Regenerate = CDT of the base plus replay (a no-op after edits); not sources-only (a) or mesh-operations-only (b). *Decided 2026-10-19: (c).*
2. **Sculpt over source contours:** moved contour vertices are clipped out of the current sources (they become edited ground); not "skip contour vertices" or "re-derive contours". *Decided 2026-10-19: clip.*
3. **Pad grading:** daylight by separate cut and fill slopes to the surface (default 1:2, maximum distance 100 m), with SketchUp's fixed-width band as a second mode. *Decided 2026-10-19: both, daylight the default.*
4. **Stamp linkage:** one-shot; the entry copies the footprint and offers Restamp; no live link to the building. *Decided 2026-10-19: one-shot.*
5. **Drape result:** imprint into the terrain by splitting triangles only (shape unchanged, optional persistent region paint) as the default, plus overlay as ordinary geometry in a group. *Decided 2026-10-19: both, imprint the default.*
6. **Protected regions and outline:** sculpt and smooth never move pad vertices; contour edits, stamps and imprints that enter a pad or leave the terrain's outline refuse with a location. *Decided 2026-10-19: yes.*
7. **Paint on edited tiles:** rebuilt tiles keep tile-level material; per-face paint on them is dropped with a notice; paint that must survive edits is a draped region. *Decided 2026-10-19: yes.*
8. **Format:** amend the unshipped `terrain-v1` in place (source IDs, tiling and tile keys, `edits`, key-order digest) instead of adding schema 30. *Decided 2026-10-19: amend in place.*
9. **Assistant visibility:** no new assistant tools; MCP exposes every R096 command; `terrain.describe` reports edits. *Decided 2026-10-19: yes.*
10. **New budgets:** one-tile commit p95 ≤ 100 ms; contour-edit preview p95 ≤ 100 ms for cavities up to 2,048 triangles; replaying Regenerate ≤ 3 s at 250k with progress and cancel; at least 64 undoable one-tile edits. *Decided 2026-10-19: adopt, to be confirmed by R096.a's measurement before they are frozen.*

## Implementation notes (R096.a)

R096.a is split into two stacked pull requests at a clean seam. **Part 1** (this section's
"Part 1" items): the `terrain-v1` amendments, structural sharing and the history charge, the edit
mesh and its grid index (also behind `terrainElevationAt`), stable tiling and local tile
replacement, the generalised tile-ID rule, the seam checker, the edit-kind registry with
incremental application, and `terrain_edit_benchmark`. **Part 2**: the replay engine with
`terrain.reset_edits`, `terrain.remove_edit` and replay in `terrain.regenerate` and
`terrain.set_parameters`, progress and cancel for replay, and the per-tile surface display and
contour caches.

### Part 1

Modules: `src/core/terrain_records.hpp` (record types), `src/core/terrain_edit_mesh.*` (the edit
mesh and its cache), `src/core/terrain_edit.*` (stable tiling, local replacement, the commit, the
seam checker, edit kinds), `src/core/terrain.*` (validation, the digest, creation, the tile-ID
rule, `terrainElevationAt`), `src/core/model.cpp` (the history charge), `src/io/terrain_io.*` and
`src/io/document_io.cpp` (the format), `tests/terrain_edit_test_kinds.hpp` (test-only kinds).

**Format** (decision 8; [ADR 0108, R096.a addendum](0108-public-native-format.md#schema-29-terrain-and-site-origin-r095a)).
`TerrainSource::id` (u32, 1..n in canonical order: the importers and `terrain.create_from_edges`
number them in the dry run, and `createTerrain` / `regenerateTerrain` number a caller's record
whose ids are all zero, so that ids equal the adapter's `sourceId`). The record's `tiling
{origin: [x, y], cell}` is the creation grid; each tile body carries `Body::tileKey` (row, column,
path, part), written as `terrainTile: {key: [...]}`. `edits` is written only when not empty. The
generator constant is `cdt-2.0.0/terrain-v1+edit-v1` for every terrain created or regenerated by
this build, edited or not (the amended format's identity). The digest reads tiles in key order
(`terrainMeshDigest` sorts its input by key, stably).

- *Choices where the ADR is open.* The edit-list bound "8 MiB of encoded edit data" is checked on
  the decoded entries' storage (`TerrainEditData::bytes`), which every kind defines, rather than
  on JSON text; the bound counts each entry's storage plus a fixed per-entry overhead
  (`terrainEditEntryBytes`), and the edit command checks exactly what validation checks, so an
  entry over it refuses with `LIMIT_EXCEEDED`. Entry ids come from a stored counter, the record's
  `nextEdit` (written when above 1; it must exceed every entry's id): an id is never reused, not
  even after the last entry is removed or the list is reset, and the counter's last value refuses
  with `LIMIT_EXCEEDED` rather than wrapping. This was chosen now, while `terrain-v1` is unshipped,
  so that R096.c's added sources and their entries never need a migration. A `kind` is at most 32
  bytes. A tile key's path is at most 24 quadrant
  digits (the creation grid's own halving bound) and its part at most 9,999; rows and columns are
  within ±2^53 so JSON numbers hold them exactly.
- *Edit kinds and the format.* R096.a defines no production kind, so a production reader refuses
  any `edits` entry (unknown kind). Kinds register their operation in core
  (`registerTerrainEditKind`) and their JSON codec in io (`registerTerrainEditCodec`); an entry's
  data is an immutable `TerrainEditData` of its kind. R096.b–e add their kinds to the built-in
  list. The test-only kinds `test.lift` (raise one tile's unshared vertices), `test.split`
  (split the triangle under a point) and `test.refine` (replace it by 3^n coplanar triangles)
  are registered only by tests, the benchmark and the fuzz
  harness; they are not part of the format.
- *Development documents.* The ADR's loader branch is implemented (`deriveLegacyTerrain` in
  `document_io.cpp`): a terrain with no `tiling`, sources without ids and tiles marked `true`
  loads with ids 1..n and the creation tiling re-derived from the stored mesh (tiling the tiles'
  triangles in ID order reproduces the buckets, since each tile kept its triangle order); a tile
  matching no derived tile (a mesh changed behind its record) takes its first triangle's cell and
  the next free part. Saving writes the new form; a test strips a document to the old form and
  gets the identical encoding back. Mixed forms are refused. No checked-in fixture or corpus file
  holds a schema-29 document: the parser corpus, the fuzz seeds and every test fixture are
  generated by the current build, so they take the new form without regeneration.

**Pinned values.** No mesh digest changes: for an unedited terrain key order is ID order, and the
legacy loader derives the same keys, so the metre fixture's `c093a02e…29584` and every other
pinned digest stand. The generator string changes from `cdt-2.0.0/terrain-v1` to
`cdt-2.0.0/terrain-v1+edit-v1` (decision 8's identity); the one test that pinned it
(`terrain_import_tests`) is updated. The adapter's own identity and digests are untouched; CDT
stays vendored byte for byte.

**Structural sharing.** `TerrainRecord::sources` is a `TerrainSources`: one immutable, shared
block of the base sources (copy on write while a record is being built). Under decision 1 the base
sources never change after creation, so the block, rather than each source, is the unit of
sharing (a deviation in granularity from §2's "per source", with the same effect). Edit entries
are `shared_ptr<const TerrainEdit>`. Record equality compares contents with an identity fast path.
`terrainRecordChangeBytes(before, after)` charges both records' own fields plus only the parts one
side holds alone (the source block when it differs; entries by identity), and
`Document::applyEdit` uses it for a terrain record change (a creation or removal is still charged
in full). Display edits now charge a few hundred bytes instead of the whole record, and copying a
record (every edit freezes its bodies) copies pointers.

**Stable tiling and local replacement.** `tileTerrainTriangles` keeps its cell, buckets and order
and also returns the grid origin and each tile's key. One fix: when all 24 attempts overflow (a
cluster that splits into parts), the cell is no longer halved once more after the last attempt, so
the stored grid is the grid the buckets were built with. The tiles were already right, so no digest
changes, but before the fix the stored cell was half the bucket cell, and an edit or a legacy load
would have mapped such a terrain's triangles to the wrong leaves. Tile keys are unique within a
terrain. Terrain operations build key-unique sets, and the document's full validation (on load and
in the full oracle) refuses two tiles of one terrain with one key, naming the terrain, the key and
both tiles. `terrainTileSet` turns a mesh
into keyed tile contents. An edit kind returns a `TerrainRegionChange` (triangles removed by
tile key and index, vertices lifted in Z by exact position, triangles added by position).
`replaceTerrainRegion` rebuilds the touched **leaves** only: the leaves of tiles holding a removed
triangle or a lifted vertex, and the leaf of each added triangle's centroid, found by descending
the cell's existing quadrant structure. A leaf is rebuilt from its old triangles (parts in
order, triangles in order) minus the removed ones, with the lifts, plus the added ones in order;
over 2,048 triangles it splits into quadrants (digit = 2 × upper half + right half, computed as
`floor(ldexp((c − origin) / cell, depth))`, the creation expression scaled exactly, so a sub-cell
always refines its parent), recursively, and at depth 24 into parts in triangle order. A leaf
that falls to zero loses its tile. Quadrants are never merged back. Within a tile, vertices are
numbered in first use and merged by exact position, as at creation, so an unchanged leaf rebuilds
to the identical mesh. Cluster parts are rebuilt together with their leaf.
`applyTerrainTiles` commits the result and the new record as one labelled edit: a rebuilt tile
whose key and mesh are unchanged keeps its body and ID; any other takes a new ID in key order, the
name of the replaced tile with its key (else "Terrain tile R*r* C*c*[ Q*path*][ part *n*]"), and
the tile-level material and colour of the replaced tile with its key, else of the cell's first
replaced tile, else the terrain's shared default (decision 7); per-face paint on replaced tiles is
dropped and counted. Every other tile is untouched, pointer-identical with its ID.
`applyTerrainEdit` appends an entry and applies it this way; it refuses an unknown kind, a full
list (4,096 entries or 8 MiB, naming Reset edits and Explode terrain), a locked terrain and a
frame that is not level (`requireLevelTerrain`).

**Tile IDs on Regenerate** (the generalised rule): a new tile whose key and mesh equal an old
tile's keeps that ID; every other new tile takes a new ID in key order. R095 kept only the
leading run, so a change near the first tile renumbered almost all of them; now only the changed
tiles do (tested: one moved vertex in the first cell renumbers one of 24 tiles). The digest no
longer needs IDs in key order.

**Edit mesh.** Per terrain: tiles by key with their plan and Z bounds, a grid index over the
stored cells (each tile listed in the cells its box, widened by 1e-6 m, overlaps; a tile spanning
more than 8 cells a side is kept in a short "wide" list), and a topology built on first use:
vertices merged by exact position (as `TerrainSurfaceDisplay::key`) with their incident triangles
(adjacency is derived from incidence, which costs less memory than a half-edge map).
`insertTile` / `eraseTile` keep all of it up to date. `terrainEditMeshCache()` keeps up to eight
terrains, keyed by document identity and terrain ID. An entry is served only for the same tile
`BodyPtr`s (the entry holds them, so their addresses cannot be reused), and the cache updates a
mesh from the tiles an edit replaced (in place when no caller holds it, otherwise on a copy, under
a mutex). The viewport prunes the entries of deleted terrains with its other display caches, and
forgets a document's entries when the document is replaced or the viewport is destroyed. Nothing observable depends
on slot numbers or incidence order: lookups return triangles in (tile key, index) order.
**`terrainElevationAt`** asks the grid for candidate tiles when the terrain's world frame is a
pure translation, then runs R095's exact tile-box and triangle tests in R095's order (ascending
tile ID, then face); a translation is monotonic, so the world box is the frame box plus the
translation computed the same way, and the results are the same bits as the full scan (tested
against a reimplementation of the scan with identity, translated and rotated frames; a rotated
frame uses the scan).

**Seam checker** (`checkTerrainSeams`, C6): merged by exact plan position, every copy of a
position has one Z; every directed edge is used once (consistent orientation, edge-manifold); and
no boundary edge (used by one triangle) has another boundary vertex strictly inside it within
1e-9 m (a crack or T-junction between tiles). Every boundary edge is checked. A short edge
searches the grid cells of its box. An edge whose box spans more than 4,096 cells searches the
cells along it (with their neighbours), or every boundary vertex when that walk would be longer.
It reports counts and at most 32 located problems.

**Measured** (`terrain_edit_benchmark --edits 100`, Release, GCC, development host with 22 CPUs;
the two 250k terrains of `terrain_budget_benchmark`; `test.lift` on the 64 largest tiles in turn):

| Terrain | Commit p50 / p95 / max | of which: digest | Edit mesh cold build, memory | `elevation_at` 200 points | History per edit | Edits per 512 MiB |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| 125,000 points (249,970 triangles, 256 tiles, tiles of 1,153 triangles edited) | 42.3 / 43.9 / 48.0 ms | 30.2 ms | 26.9 ms, 22.1 MB | 9.9 ms | 0.96 MB (max 1.02 MB) | 561 |
| 416 contour rings (249,298 triangles, 3,169 tiles) | 53.5 / 56.7 / 58.7 ms | 39.9 ms | 35.2 ms, 30.3 MB | 0.7 ms | 0.47 MB (max 0.84 MB) | 1,148 |

The one-tile commit (decision 10, p95 ≤ 100 ms) is met by the core edit at 44–57 ms; the display
update and the frame are measured with part 2. Most of the commit is the whole-terrain SHA-256
(7.5 MB of input); the kind, the replacement and the cache update are under 1 ms. A per-tile hash
tree would remove it but changes the digest's definition, so it is left for when the frame budget
needs the time. All 100 edits undo; the records charge 3.0 MB once (at creation), not per edit,
and at the measured rate at least 561 one-tile edits fit the terrain budget, against the 64 that
decision 10 asks. `terrain_edit_tests` checks 80 lifts on a terrain whose record is 2.9 MB, each
charged under 1 MB, and 80 undos to the original encoding.

**Verification (part 1).** `terrain_edit_tests` (ctest `terrain_edit`, dev and sanitize presets):
stable tiling (the record's grid, keys ascending with IDs, the key-order digest equal to the ID-order
digest, source ids); the format round trip (document and container bit for bit, the JSON shape,
refusals of duplicate edit and source ids, an unknown kind, an empty or over-long list, a zero
cell, a bad key digit or fractional row, mixed forms); the development form (stripped to R095's
form, it loads and re-saves identically); local replacement on a 60,000-triangle grid (exactly one
tile rebuilt with its key, name and material, every other tile pointer-identical with its ID,
border vertices fixed, elevations at 2,000 points outside the tile bit-equal, the cache following
the edit, seams, Undo pointer-identical, Redo bit for bit, the same edit on another document
identical); 40 splits and one refinement in one cell (each one edit; the refinement pushes the
cell over 2,048 triangles and it splits into quadrants; every triangle in its tile's cell and
quadrant, distant tiles pointer-identical, seams); the seam checker's three
faults; the history charge; the generalised tile-ID rule; the indexed elevation query.
Review fixes add `clusterParts` (a terrain forced into parts stores the bucket cell and an edit in
the cluster stays in its cell), `editIdsAndBounds` (the counter, a borderline entry, the last id),
`editMeshCache` (per-document entries, pruning, forgetting), a duplicate-key refusal and a crack in
a very long boundary edge. `parser_fault_corpus_tests` and the `terrain-edits` fuzz format cover
the edit-list decoder (see
[PARSER_FUZZING.md](../PARSER_FUZZING.md)). The R095 suites pass unchanged except the generator
pin.

### Part 2

Modules: `replayTerrainEdits` in `src/core/terrain_edit.*`, `runTerrainRegenerate` in
`src/io/terrain_import.*`, the replay requests and commands in `src/automation/terrain_commands.*`,
the per-tile caches in `src/core/terrain_display.*`, their use in `src/app/viewport*.cpp`, and
`TerrainStage::Replay` in `src/geometry/terrain_progress.hpp`.

**Replay engine.** `replayTerrainEdits(base, edits, progress)` builds an edit mesh from the base
tiles and runs every entry through the same kind, `replaceTerrainRegion` and leaf rules as the
incremental edit, then returns the tiles in key order. Replay equals incremental: a replay of the
list a terrain was edited with gives the same keys, names and meshes, so Regenerate after edits is
a no-op (C7: no edit, no history entry, the same IDs and digest; tested before and after save and
reopen). An entry that no longer applies (the kind returns nothing, or its change is refused, or it
would leave no triangle) is skipped with its reason and kept in the list. `runTerrainRegenerate`
triangulates the base sources, tiles the mesh (`terrainTileSet`), replays the list, checks the
document triangle limit on the replayed tiles, and returns the tiles (`tileSet`). The report's
terrain object gains `edits {count, applied, skipped}` and an `edit-skipped` notice locating each
skipped entry as "edit N (kind)" with its reason. `regenerateTerrain` applies the tile set with the
generalised tile-ID rule, so only the tiles that differ change.

**Commands.** `terrain.regenerate` and `terrain.set_parameters` replay the list. New:
`terrain.reset_edits {terrain}` ("Reset terrain edits") empties the list and rebuilds the base;
`terrain.remove_edit {terrain, edit}` ("Remove terrain change") removes one entry by its integer ID
and replays the others. Both follow Regenerate's rules exactly (tile IDs, the paint notices, the
report), as §2 describes ("Reset edits is Regenerate with the list emptied"). Both keep the
record's edit-ID counter, so a later change never takes a removed change's ID. Removing the last
entry also replays rather than restoring the state before it: the result is the same by replay ==
incremental, and there is one code path. Refusals: a missing terrain or change is `NOT_FOUND`; a
reset with no edits and a non-integer `edit` are `INVALID_REQUEST`; a locked terrain is
`INVALID_OPERATION`, and so is a terrain that is not level when edits remain. Each refusal changes
nothing. The commands are in the catalogue with dry run and revision checks, MCP exposes them, and
`assistantVisible` hides them (decision 9; the tool-surface baseline is unchanged). The API schemas
and the tool reference are regenerated. `terrain.describe` reports `edits {count, bytes, space:
"terrain", entries: [{id, kind, bounds?}] (at most 200), entriesOmitted}`. The assistant's
description of `terrain.describe` is not changed yet: no production edit kind exists, and the clause
of decision 9 belongs with the first one (R096.b). That keeps the pinned baseline.

**Progress and cancel.** The four replaying commands split, like the import, into
`parseTerrainReplayRequest` (on the document), `runTerrainReplay` (reads no document, so a desktop
worker can run it) and `applyTerrainReplay` (one labelled edit). `runTerrainReplay` takes a
`TerrainProgress`: it reports Triangulate and Tile (the base tiling included) through the D-044 checkpoints (the CDT abort hook
included) and the new `TerrainStage::Replay` once per entry. `terrainReplayProgress` weights them
40 %, 5 % and 55 %, and the desktop stage text is "replaying the edits". A cancelled run returns
`cancelled` with code `CANCELLED` and nothing kept. `applyTerrainReplay` refuses a cancelled or
failed run, and also a request whose terrain record changed since it was made (`INVALID_OPERATION`).
The batch commands run the three parts in sequence without progress. Desktop wiring of Regenerate
and Terrain settings to a worker with a progress bar is left to R096.f, which adds the Edits list.
No production terrain has edits before R096.b, so those dialogs replay nothing until then.

**Per-tile display.** `TerrainSurfaceCache` keeps a `TerrainTileSurface` (normals and outer edges)
per tile and computes it from the edit mesh's incidence. After an edit it recomputes only the
rebuilt tiles and the tiles that share a position with them (the seam ring). A tile whose record
changed but whose mesh did not (paint) keeps its surface. Every other tile keeps its object, and the
viewport's per-body cache compares that pointer, plus the elevation range for band shading, so it
re-tessellates only those tiles. `terrainSurfaceDisplay` (the whole-terrain form the tests use)
now sums each position's normals in tile-key order, as the per-tile form does, and the two are
bit-equal (tested before and after an edit). For a terrain never edited this is the R095 order.
`TerrainContourCache` keeps segments per tile, by record or by mesh, and assembles the terrain's
segments in key order. Only rebuilt tiles are regenerated. The 500,000-segment bound applies to the
sum of the per-tile estimates, which are whole numbers and so exact. When every tile is reused the
previous contours are returned. The R095 cache test now counts tile generations instead of digests;
its digest counter is gone.

**Choices where the ADR is open.** Names and paint follow part 1's rules, so a user-renamed tile
can differ between a replay and the incremental result (names are not in the digest; C7 compares
IDs, records and digests). Desktop progress UI is deferred as above. The replay bound "≤ 3 s at 250k
with 1,000 entries" is measured below.

**Measured** (`terrain_edit_benchmark --edits 100 --replay 1000 --cancel 20`, Release, same host;
display = `TerrainSurfaceCache` and `TerrainContourCache` with 1 m contour lines, without GPU upload
or drawing):

| Terrain | One-tile commit p50 / p95 | Display update p50 / p95 | Commit + display p95 (max) | Replaying Regenerate, 1,000 entries | Cancel latency p50 / p95 / max |
| --- | ---: | ---: | ---: | ---: | ---: |
| 125,000 points (256 tiles) | 38.0 / 41.1 ms | 8.9 / 10.6 ms | 50.2 ms (62.9) | 1.12 s (request 7 ms, CDT and replay 827 ms, apply 290 ms) | 7.7 / 34.7 / 51.2 ms |
| 416 contour rings (3,169 tiles) | 56.3 / 61.5 ms | 17.6 / 19.7 ms | 80.7 ms (96.3) | 1.02 s (13, 705, 303 ms) | 11.6 / 26.4 / 43.2 ms |

The one-tile edit from release to committed state is within the 100 ms of decision 10 at p95
(50–81 ms) before the GPU upload and the frame. R095.f measured a complete frame at 250k at p95
0.79–0.94 ms on the AMD GPU, but re-uploading the contour batch (all segments; the GPU batches stay
per terrain) and the rebuilt tiles is not measured here and belongs to R096.f's
input-to-frame row. The commit is dominated by the whole-terrain SHA-256: 27 ms and 41 ms here,
after appending its 7.5 MB input in bulk instead of byte by byte, which saved 4–5 ms. A replaying
Regenerate with 1,000 entries takes 1.0–1.1 s against the proposed 3 s, and a cancel stops it
within 52 ms. The cold display build (first draw) is 211–335 ms, once per terrain.

**Verification (part 2).** `terrain_replay_tests` (ctest `terrain_replay`, dev and sanitize
presets), on terrains imported from a generated 12,000-point file, so the base sources reproduce
the base mesh:
- replay == incremental for 12 edits (lifts, splits, and a refinement that splits a cell into
  quadrants), with the same keys, names and meshes. Regenerate is then a no-op: in the core, through
  the batch, and after reopen. Two documents with the same edits are identical.
- `terrain.reset_edits` returns the base digest and tiles, keeps the untouched tiles' bodies, and
  undoes (tiles pointer-identical) and redoes. A reset with no edits refuses.
- `terrain.remove_edit` of a middle entry equals never having made it (same tiles and digest), the
  other entries keep their IDs, and undo and redo work. Unknown, malformed and locked requests refuse
  and change nothing. Removing a refinement skips a later lift of a quadrant tile, reports it and
  keeps it.
- `terrain.set_parameters` replays, and `terrain.describe` lists the edits.
- Progress moves forward to 1 and does not change the result. A cancel at each replay checkpoint
  returns `CANCELLED`, is not applied, and leaves revision, history and the document unchanged. A
  stale request is refused.
- After a one-tile edit, 8 of 30 tile surfaces are recomputed, the others are pointer-identical,
  and everything is bit-equal to the whole-terrain surface. One tile's contours are generated, and
  the assembled contours equal the whole-terrain ones.

`command_tests` covers the two new commands, and the R095 desktop and display suites pass.
The edit-list decoder was fuzzed with libFuzzer (clang, ASan and UBSan, the documented options):
`parser_fuzz_terrain_edits` ran 45,407 inputs in 180 s from the generated seeds (522 coverage
points, 581 MB peak) with no failure; `parser_fuzz_seeds` generates its two seeds.

## Implementation notes (R096.c)

Modules: `retriangulateTerrainCavity`, `flipTerrainFlatTriangles` and `terrainInCircle` in the
adapter (`src/geometry/terrain_triangulation.*`, still the only file that includes CDT; the core
now links the adapter library), `src/core/terrain_sources.*` (the current sources, their index and
cache, the cleaning shared with the importer, check C1), `src/core/terrain_source_edit.*` (the
three kinds, the local re-triangulation, promotion, the located refusal), the kind hooks and
`previewTerrainEdit` in `src/core/terrain_edit.*`, the codecs in `src/io/terrain_io.cpp` and the
commands in `src/automation/terrain_commands.cpp`.

**Cavity API.** `retriangulateTerrainCavity` takes the kept vertices with Z and a contour flag, the
cavity's boundary as directed edges (cavity on the left; any number of loops, touching at vertices
allowed), the kept constraint segments inside it, and the new contours, breaklines and points; the
vertices to remove are simply left out. It runs the adapter's own steps: numbers and bounds, the
vertex store (fixed vertices first), segment merging, the exact crossing check (`findCrossings`,
now shared with `triangulateTerrain`: a vertex inside a boundary edge or a constraint is refused,
boundary edges are never split), the CDT of the vertices in (x, y) order with the edges sorted, so
the result does not depend on the caller's numbering, the triangles inside the boundary by a flood
from its edges (an inconsistently oriented or open boundary, or one open to the hull, is
`invalid-polygon`), and the flat-triangle flip (`flipFlats`, shared) inside the cavity only. Its
output keeps the input vertices first and names, for each new vertex, the vertex it became
(`placed`). Two choices beyond §3: a new vertex merges into another within 1e-7 m only with a
bit-equal elevation (C1 is exact), and a new vertex outside the cavity is `invalid-polygon`.
Steiner fill and the edge-length trim do not run (§10, decision 6). The refactor of
`triangulateTerrain` (two shared functions) changes no output: the pinned digests stand.

**The kinds.** `editSource {source, role, points, before}`, `addSource {source, role, points}` and
`removeSource {source, before}` ([ADR 0108 addendum](0108-public-native-format.md#schema-29-terrain-and-site-origin-r095a)).
An entry stores its resolved geometry; the geometry it replaces is read from the current sources
at its place in the list. `TerrainEditKind` gains three optional hooks: `applyInContext` (the mesh,
the current sources before the entry and the parameters), `sources` (the entry's effect on the
current sources, mesh-free) and `protects` (the plan polygons the entry protects; R096.d's pads
plug in here). The current sources of a record are its base sources with every entry's effect in
order (`terrainSourceState`); the replay derives them the same way as it goes, so an incremental
edit and its replay see the same sources. They are cached by base block and entry identity, and a
record that extends a cached one by entries is derived from it in place, so a preview, its commit
and the next preview do not rebuild the index (19 ms cold at 250k). Source ids: an added source
takes the largest base id plus its entry's id, so ids are never reused (entry ids are not), even
after Reset edits; the record check refuses an `addSource` entry whose id breaks this rule, so a
file cannot claim an id a later entry will take.

**Re-triangulation** (`planTerrainSourceEdit`). Elevation-only (same plan positions in order): the
changed vertices are lifted and `flipTerrainFlatTriangles` runs over their incident triangles (all
the source's when its role changes); with no flip the change is lifts only. Otherwise the old and
new points are matched by exact plan position: a vertex only the old geometry has is removed unless
another current source uses it or it is on the outline or a hole boundary (then it stays as a plain
vertex, so the outline never changes); a vertex both have with a new Z is lifted; a new one is
inserted, merging with an existing vertex of the same Z, or replacing a plain (non-source) vertex
at its position. The cavity is every triangle touching a removed or lifted vertex, a removed
segment's edge (when no other source keeps it), an inserted vertex or a new segment (closed
exact tests), plus each inserted vertex's Bowyer–Watson cavity (exact in-circle), never across a
current constraint, the outline or a protected region. A plain vertex (one no current source
holds, such as a Steiner point or a vertex an earlier edit left, and not on the outline) lying
exactly on a new segment is removed with the cavity rather than refused as a touch. An edit that
changes no triangle (a spot point removed from the outline) changes only the record.

**Cavity bound.** The cavity is bounded while it is collected: an edit whose cavity would pass
65,536 triangles (`terrainSourceEditCavityTriangles`) is refused with `LIMIT_EXCEEDED`, located at
the geometry that crossed the bound, with "split it into smaller changes"; on replay such an entry
is skipped as soon. A second bound beside it counts the triangles found touching the new geometry,
once per vertex and segment (16 × 65,536): a zigzag of long, nearly parallel segments keeps its
cavity small while every segment rescans it, and is refused the same way. Segments find their
triangles through the grid index, a box of at most one cell per step along the segment, with a
cheap side-of-line test before the exact one. Measured (Release, the 250k point terrain): a
11,601-point serpentine breakline with a 54,865-triangle cavity previews in 0.62 s; the worst
case, a 20,000-point zigzag whose every segment spans the terrain, is refused in 0.82 s (3.0 s
with the cavity bound alone; before either bound and the grid walk it scanned every triangle in
each segment's box, about 5e9 orientation tests). The bound allows any
realistic reshape: a 290-vertex ring move is 1,171 triangles, and a 2,000 m breakline about 2,000.

**Validation.** Crossings, touches, conflicting duplicates and the adapter's limits come from the
cavity call: every current contour and breakline segment is a mesh edge (C1), so any segment a new
segment could cross is an edge of a triangle it touches, which is in the cavity. The outline rule
(decision 6) is checked before: a new vertex must lie in a triangle and not on an outline or hole
edge, and a new segment must not meet an outline or hole edge except at its own ends, nor run
along one (a collinear overlap, including a segment equal to an outline edge), nor have its middle
off the terrain (between two outline points across a notch or a hole) (`outside-outline`). The pad rule checks the new, removed and lifted geometry against every
protected polygon (`inside-pad`; tested with a test-only kind until R096.d). Cleaning is the
importer's (`cleanTerrainPolyline`, `canonicalTerrainPolyline`, moved to core and shared).
Refusals are `TerrainEditRefused` with the import's report kinds and locations; the commands
return them as `terrainReport` (the import's bounded report, `format: "terrain-edit"`, locations in
model coordinates) with `INVALID_REQUEST`, or `LIMIT_EXCEEDED` for the limit kinds. A refusal
changes nothing.

**Commands** (§8). `terrain.edit_source {terrain, source, points?, elevation?, role?}` ("Edit
contour"), `terrain.add_source {terrain, role, points | edges | level, elevation?, at?}` ("Add
contour") and `terrain.remove_source {terrain, source}` ("Remove contour"), each one labelled edit
through the batch API with its dry run, MCP and sessions; `assistantVisible` hides them by their
`terrain.` prefix, so the tool-surface baseline is unchanged (decision 9). Points are model
`[x, y, z]` (or `[x, y]` for a contour with `elevation`); a closed polyline repeats its first point.
Boundary and hole sources are not edited (they define the outline); spot points keep their role.
The command dry-runs the plan, stores the resolved geometry (a vertex merged within 1e-7 m is
stored where the mesh holds it) and commits; the result names the source, the entry, the tiles and
the cavity, with notices for cleaning, merges, remaining flat triangles and dropped face paint.
`terrain.describe` adds `currentSources {count, vertices, roles, entries (id, role, reference,
points; at most 200), entriesOmitted}` for MCP and sessions; its `edits` entries carry their boxes
from the kinds. The in-app assistant reads `terrain.describe` without `currentSources`
(`assistantResult` drops it from its results and its initial context): it names ids no
assistant tool takes, and the result stays the size R095.g measured. The assistant's schemas,
`assistant_tool_surface_tests` and corpus v5's `toolsSha256` are unchanged.

**Large point sets.** An entry stores the whole new geometry, about 24 bytes a point, at most
20,000 points; at that rate about 28 edits of a 12,000-point set fill the list's 8 MiB. Editing a
source of more than 20,000 points (one imported point file) is refused with `LIMIT_EXCEEDED`:
"This point source has N points; changes are limited to 20,000 points per source. Split it with
Explode terrain or re-import it in parts." Spot points can still be added, and the set removed.
Storing per-point changes instead of the whole set is a logged follow-up.

**Promotion.** `terrain.add_source {level, at?}` promotes the surface's contour line at a model
elevation: the plane cuts each triangle with a vertex at the level counted as above (as the
display contours do), crossing points are interpolated in canonical endpoint order (an endpoint at
the level is the vertex itself), and the segments chain into lines. With several lines `at` picks
the nearest, otherwise the request is refused; an open line's ends on the outline are dropped
unless they are mesh vertices, so the contour stays inside the outline. The promoted contour then
goes through the ordinary add. A display contour that crosses a breakline (or another contour) is
refused: where it crosses, its point lies on the breakline, which the crossing rule refuses as a
touch. The refusal says so ("The contour line at this level crosses a breakline or another
contour, which a source contour may not touch"); splitting the breakline there is not offered.

**Previews** (§5). `previewTerrainEdit` computes exactly what `applyTerrainEdit` would, on the
cached edit mesh and current sources, without validating or committing; a test checks that the
committed tiles equal the preview bit for bit. The desktop path is R096.f's.

**A defect found and fixed.** Qt's JSON writes −0.0 as `0`, so a terrain whose imported sources
held −0.0 (a DXF `-0.0000`) regenerated after save and reopen to a mesh whose digest differed
(C7 failed). Import now stores such coordinates as +0.0 (the same value); no pinned digest changed.

**Choices where the ADR is open.** (1) A source entry skipped on replay (after Terrain settings
move the outline from under it, or after removing an earlier entry it depended on) still counts in
the derived current sources, as every derivation applies every entry; C1 as amended leaves it out
(`checkTerrainSourceConsistency(doc, terrain, skipped)` takes the skipped ids), and the replay's
`edit-skipped` notice names it. (2) Large point sets: see above. (3) The assistant's
`terrain.describe` description and results are unchanged (pinned baseline; decision 9's clause
about `edits` stays with the first assistant-facing change).

**Measured** (`terrain_edit_benchmark --edits 100 --replay 1000 --cancel 20 --source-edits 100`,
Release, GCC, the development host with 22 CPUs, the two 250k terrains):

| Terrain | Contour-edit preview: cavity triangles, p95 (max) | One-tile commit p95: core / command / command + display (max) | Current sources, cold |
| --- | --- | --- | ---: |
| 416 contour rings (249,298 triangles, 3,169 tiles) | 1 to 290 moved vertices of a ring: 14–1,171 triangles, 0.75–9.5 ms (37.2, the first) | 58.7 / 59.6 / 79.8 ms (102.5) | 19.4 ms |
| 125,000 points (249,970 triangles, 256 tiles) | a spot point: ≤ 8 triangles, 0.22 ms; breaklines of 50–2,000 m: 39–1,975 triangles, 0.64–16.1 ms; 2,700 m: 2,520–2,659 triangles, 21.9 ms | spot point, core: 42.6 ms | 8.5 ms |

Both budgets of decision 10 hold with margin: the contour-edit preview is at most 16.1 ms at p95
for cavities up to 2,048 triangles (budget 100 ms), and a one-tile contour commit is 58.7 ms at p95
in the core and 79.8 ms with the command and the display update (budget 100 ms; the largest single
sample was 102.5 ms). The commit is still dominated by the whole-terrain digest (R096.a); the plan
and the cavity are under 1 ms for one moved vertex. After the commits the benchmark checks C1 over
the 416 contours (124,800 vertices and segments) and C6 at 250k. The R096.a rows are unchanged
(one-tile `test.lift` commit p95 40.0 and 59.9 ms; replaying Regenerate of 1,000 entries 1.10 and
1.01 s).

**Verification.** `terrain_source_edit_tests` (ctest `terrain_source_edit`, dev and sanitize
presets) on a 40-ring contour hill (12,638 triangles), the 12,000-point terrain, a 120 × 120 m
gridded point terrain (co-circular everywhere) and the R095 contour fixture: moving one vertex (a
15-triangle cavity; the preview equals the commit), re-elevating a ring (160 lifts, no
re-triangulation) and raising it to its neighbour's elevation (flats), contour to breakline,
adding a contour, a breakline and spot points, removing an added and a base source, promoting a
display contour, a scripted sequence of all of them, Reset edits and Remove change of a source
edit (ids never reused). After each: C1 (`checkTerrainSourceConsistency`), C2 (display contours
through every source contour vertex where the ground falls below its level, and the per-tile cache
equal to a fresh build), C5 (kept tiles pointer-identical, 1,600 elevations outside the rebuilt
tiles bit-equal), C6 (`checkTerrainSeams`) and C7 (Regenerate changes nothing), also after Undo and
Redo, after save and reopen, and on a second document (identical). The sequence's replay digest is
pinned. Refusals (crossing, a vertex moved across a ring, outside the outline, into the pond, a
conflicting duplicate, inside a protected region, unknown and boundary sources, a contour at two
elevations, no line at a level, a locked terrain) are located and change nothing.
`terrain_triangulation_tests` adds the cavity API (insertion, merge, canonical order, the refusal
kinds, flats in the cavity and the elevation-only flip) and 3,000 generated cavities through the
new `terrain-cavity` harness; `command_tests` covers the three commands; the parser fuzz seeds and
`parser_fault_corpus_tests` cover the source-edit codecs. Review fixes add: the cavity bound on a
79,202-triangle grid and the work bound on a 20,000-point zigzag (each refused, located, quickly);
the assistant's `terrain.describe` without `currentSources` (context and tool result) while the
shared inspection keeps it; a 21,000-point source refused with `LIMIT_EXCEEDED`; an `addSource` id
off the rule refused at record check; a breakline through a plain vertex (C1, C6, C7); a breakline
along the outline refused; a promotion across a breakline refused with its message; and C1 with a
skipped entry left out.

Fuzzing (libFuzzer, clang 23 with ASan and UBSan, the documented options, 2026-10-20):
`parser_fuzz_terrain_cavity` ran 271,021 inputs in 300 s from the nine generated seeds (6,131
coverage points, 508 MB peak) and `parser_fuzz_terrain_edits`, now with the production source
edits in its seeds, ran 196,342 inputs in 180 s (871 coverage points, 572 MB peak), both with no
failure.

## Implementation notes (R096.b)

Modules: `src/core/terrain_sculpt.*` (the kinds `sculpt` and `smooth`, the weight function, the
source clipping), `TerrainSourceState::clipPoints` in `src/core/terrain_sources.*`, the codecs in
`src/io/terrain_io.cpp` and `terrain.sculpt` in `src/automation/terrain_commands.cpp`. The kinds
are built in next to R096.c's, and `terrain_sculpt.cpp` is compiled with `-ffp-contract=off` like
the other replayed code.

**The kinds** (§4.1, §7). `sculpt` (raise; a negative amount lowers) and `smooth` share one entry
type: a brush `{path, radius}` or a region `{region, falloffWidth}`, the `falloff`, and `amount` or
`strength` and `iterations`, all in the terrain frame
([ADR 0108 addendum](0108-public-native-format.md#schema-29-terrain-and-site-origin-r095a)). An
entry stores its parameters, not the vertices it moved: replay recomputes them on the mesh before
it, as for every kind. Bounds: a path of 1 to 256 points, a radius of 0.01 to 1,000 m, a region of
3 to 20,000 corners, a width of 0 to 1,000 m, a strength in (0, 1] and 1 to 8
iterations, as §4.1 gives them (a region's corners not all on one line, by exact orientation,
and at most 512 edges in a band); the amount is a choice where the ADR is open: non-zero and at most
1,000 m either way.

**Weights.** `TerrainSculptInfluence::weight(x, y)` is the one definition of the weight, used by the
mesh change, by the source clipping and by previews, so they agree bit for bit. For a brush, d is the
plan distance to the nearest segment of the path; each segment is measured from its
lexicographically smaller end, and a projection outside the segment uses the end point exactly, so
a reversed path or one with repeated samples gives the same bits, and collinear resampling agrees
within 1e-12 (tested: a 256-sample stroke and a two-point stroke give surfaces within 1e-9 m, here
identical). Lengths use the correctly rounded `sqrt`, not `hypot`, so no C library's last bit
enters replay. f is `1 − t²(3 − 2t)` (smooth), `1 − t` (linear) or 1 (constant), with t = d / r,
and zero at t ≥ 1: a vertex exactly at the radius or the width does not move (tested on a 1 m grid
for each falloff, and for a region's outline, which is inside). A region is the nonzero-winding
interior with the adapter's exact orientation, closed. Its edges are indexed twice: horizontal bands
for the winding test (min(4,096, corners) bands; a vertex reads its band only) and a
bounding-volume tree for the distance outside (a nearest-edge search that skips a node only when
it is clearly, beyond rounding, farther than both the best distance and the width, so the result
does not depend on the tree), tested against brute force on a 20,000-corner star. A region whose
busiest band holds more than 512 edges is refused (see the review fixes below).

**Raise and smooth.** The moving vertices are those of the tiles in the influence box with a
weight above zero, each position once, in (x, y) order. Raise lifts each to z + amount × w; a
stroke is swept, so 256 samples along one line move no vertex more than the amount. Smooth runs
Jacobi iterations over them: the mean is the plan inverse-distance (1/d) weighted mean of the one
ring, summed in (x, y) order; neighbours without weight are read and never written (tested against
an independent long-double computation). Only Z changes (`TerrainRegionChange::lifted`); the plan
triangulation is unchanged (tested), and the flat-triangle flip does not run (§4.1). A change that
moves no vertex is refused ("reaches no terrain vertex", "moves no terrain vertex", or "on a pad")
with a located report, and on replay is skipped with that notice.

**Pads** (decision 6). Vertices on or inside a polygon of the `protects` hook (R096.c) weigh zero;
they are neither moved nor clipped. Pads arrive with R096.d, so the tests use a test-only kind with
`protects` (as R096.c's pad rule test does), with a codec so the documents reopen. Removing the pad's
entry lifts the protection: the replayed sculpt moves those vertices too (tested).

**Contours** (decision 2). A kind's `sources` hook clips the current sources without the mesh: every
contour, breakline and spot-point vertex with a weight above zero and no pad over it is edited ground
now. The clipped set contains every source vertex the change moves, since a moved vertex has a
weight and no pad; it can contain a few the change leaves where they were (a smooth that finds a
vertex already at its mean), which is harmless for C1. So C1 holds for the remaining sources. Choices
where the ADR is open:
- *Lines.* A contour or breakline keeps its id for its first remaining run (in point order; a ring
  from its first clipped vertex, so a closed ring becomes an open run); each further run is a new
  source of the same role and reference; runs of one vertex are dropped.
- *Ids of further runs.* They come from a block per entry, 2^32 − 1 − (entry − 1) × 4,096 − n, so
  they are never reused, like added sources' ids. The command records each further run's (line,
  id) in the entry (`pieces`, resolved geometry): on replay a line's runs take its recorded ids
  first, so a piece keeps its id, and a later edit that names it still finds it, when an earlier
  change is removed and the sculpt now splits other lines too (tested). Base and added ids stay
  below the block range (3,221,225,472; the record check refuses a base id above it). An entry
  that would split more than 4,096 runs, or whose id is past 2^18 (262,144 changes in a terrain's
  life), and that must split a line is refused with `LIMIT_EXCEEDED`.
- *Spot points* lose the clipped points (`clipPoints` updates the index for the dropped positions
  only, so a large point source is not re-indexed). Boundary and hole sources are not clipped: they
  define the outline, whose plan never changes; their stored Z is the import's (§4.1: "the outer
  boundary may move in Z").

The command reports the clipped vertices, the sources touched, the further pieces and the
sources left empty, and `terrain.describe` lists the pieces with the other current sources.

**Command** (§8). `terrain.sculpt {terrain, mode: raise|smooth, path | region, radius?, falloff?,
falloffWidth?, amount?, strength?, iterations?}`, labelled "Sculpt terrain" or "Smooth terrain",
one edit through the batch API with its dry run, MCP and sessions. Defaults: falloff smooth, width
0, strength 0.5, one iteration. A region may repeat its first point at the end. Fields of the other
mode or scope, a missing radius or amount and every bound are refused (`INVALID_REQUEST`, or
`LIMIT_EXCEEDED` for the point counts) and change nothing (tested one by one, also through the
batch). Inputs are model metres stored as they are, so a terrain whose frame is scaled is refused
(`INVALID_OPERATION`); terrains are created unscaled, and a moved or turned terrain is fine. The
bounded terrain report (`format: "terrain-edit"`) carries `verticesMoved`, `verticesInfluenced`,
`verticesProtected`, `tilesTouched`, `tilesAdded` and `sourcesClipped {sources, vertices, pieces,
removed}`, with notices `sources-clipped`, `pad-protected` and `face-paint-dropped`. The in-app
assistant gets no tool (decision 9): `assistantVisible` hides `terrain.` commands, the assistant's
schemas, `assistant_tool_surface_tests` and corpus v5 are unchanged, and `terrain.describe` needs
no filtering since its shape did not change (its `edits` entries now name `sculpt` and `smooth`).
Decision 9's clause about `edits` in the assistant's `terrain.describe` description stays deferred,
because adding it would change the pinned tool surface.

**Measured** (`terrain_edit_benchmark --edits 100 --replay 1000 --cancel 20 --source-edits 100
--sculpts 100 --sculpt-replay 1000`, Release, GCC, the development host with 22 CPUs, the two 250k
terrains). A one-tile sample is a three-point stroke through the middle of one of the 64 largest
tiles, its radius a quarter of the tile's smaller side, raising 0.01 m; samples whose touched tiles
hold more than 2,048 triangles in all (4 of 100 on the point terrain, where a stroke near a seam
touches two large tiles) are left out of the budget rows. The nine-tile brush is a click at a
tile's middle with a radius of 1.1 cells. Display = the per-tile surface and contour caches, without
GPU upload or drawing.

| Terrain | One-tile: vertices, touched triangles (p95) | Preview p95 (max): raise / smooth ×3 | Commit p95: core / command / command + display (max) | Nine tiles: vertices, triangles; preview / commit / display p95 | Replaying Regenerate, 1,000 sculpts |
| --- | --- | --- | --- | --- | ---: |
| 125,000 points (256 tiles) | 229; 2,021 | 3.8 (4.1) / 0.7 ms | 38.7 / 40.1 / 48.6 ms (54.0) | 2,025; 9,484; 6.6 / 101.1 / 27.8 ms | 1.55 s |
| 416 contour rings (3,169 tiles) | 177; 926 | 2.6 (4.0) / 1.1 ms | 65.9 / 65.2 / 85.0 ms (89.8) | 1,509; 6,246; 5.0 / 103.3 / 33.4 ms | 1.35 s |

The one-tile commit holds decision 10's 100 ms at p95 (39–66 ms in the core, 49–85 ms with the
command and the display update); as before, most of it is the whole-terrain digest. The preview
computation is at most 4.1 ms, small enough for the UI thread as §5 plans. A brush over nine tiles
previews in 5–7 ms and commits in about 100 ms p95 (rebuilding and validating nine tiles of up to
9,500 triangles); with its display update it is about 130 ms, which R096.f's input-to-frame row
measures with the frame. A replaying Regenerate of 1,000 sculpt and smooth entries takes 1.35–1.55 s
against the proposed 3 s; each entry clips the current sources twice (in the change and in the
derivation), a linear scan of the source vertices that is a small part of it. After the commits C1
holds over the contours' 960 current sources (119,071 vertices, the rings now split around the
strokes) and C6 at 250k. One earlier run on the same host, while the machine was swapping, measured
the contour terrain's one-tile command and display at 139.5 ms p95; a repeat on the quiet machine
gave the figures above, and a sculpt-only run gave 69.7 ms. The R096.a and R096.c rows are
unchanged: a one-tile `test.lift` commit p95 42.0 and 64.3 ms; a contour edit commit 61.4 ms; a
replaying Regenerate of 1,000 lifts 1.10 s on the point terrain when the benchmark runs it alone
(1.64 s after the other sections in the same process) and 0.99 s on the contours.

**Review fixes.** An independent review of the pull request found no blocker; these follow it.
- *Hostile regions.* A comb of 19,999 corners crossed every horizontal band about 15,000 times,
  so each vertex's inside test read nearly every edge, and the distance grid's cells grew to the
  comb's height: on a large terrain a replay would have hung. Now the busiest band (of min(4,096,
  corners)) may hold at most 512 edges, counted without building the bands, and the validator,
  the decoder and the command refuse anything more (`LIMIT_EXCEEDED`, the command's report
  located at the busiest band). The distance outside uses a bounding-volume tree instead of the
  grid. Measured on the 250k terrains (the same benchmark, `hostileRegion`): the 19,999-corner
  comb is refused in 2.3–2.4 ms; a 128-tooth comb at the bound (515 corners, 385 edges in its
  busiest band) over 80 % of the terrain previews in 336 ms (points, 85,928 vertices moved,
  210 tiles) and 495 ms (contours, 113,080 vertices, 2,655 tiles), at most a band's edges a vertex.
- *Cancel inside an entry.* `TerrainEditContext` carries the replay's progress, and the sculpt
  plan checks for cancellation every 4,096 vertices it scans and every smoothing iteration, so a
  Regenerate, Terrain settings or Remove change is cancelled part-way through a long entry
  (tested: a cancelled plan throws `TerrainCancelled` inside a 12,000-vertex smooth).
- *Exact area.* A region's corners must not all lie on one line, by the adapter's exact
  orientation, in place of a `long double` area that is 80-bit on x86 Linux but 64-bit elsewhere.
  A bow-tie (zero signed area) is a valid nonzero-winding region.
- *Pieces from their own block.* A recorded piece whose id is not in its entry's block is refused
  when a document loads (`validateTerrainRecord`) and by the hook.
- *Pads.* The protected polygons' boxes are computed once per plan, not per vertex.
- Not done: the command still plans a sculpt twice (its dry run for the report, then
  `applyTerrainEdit`). The dry run is the preview's cost (at most 4.1 ms above); R096.f's preview
  path can pass the previewed plan to the commit.

Re-measured after the fixes (same command, same host): a one-tile sculpt commit p95 41.5 and
67.3 ms in the core, 54.0 and 88.9 ms with the command and the display update (points and
contours); the preview p95 3.9 and 2.6 ms; nine tiles commit p95 118.6 and 104.7 ms; 1,000 sculpts
replay in 1.66 and 1.40 s.

**Verification.** `terrain_sculpt_tests` (ctest `terrain_sculpt`, dev and sanitize presets) on the
R095-style 40-ring contour hill, the 12,000-point terrain and a 40 × 40 m gridded point terrain:
- the weight function (falloff values and edges, sampling, reversal, the region indexes against
  brute force);
- a raise along a stroke over the hill: every vertex moves by exactly amount × w, the plan is
  kept, the crossed rings are clipped into runs with split ids, the preview equals the commit, the
  report counts the vertices and tiles, and C1, C2, C5, C6 and C7 hold, also after Undo
  (tiles pointer-identical), Redo, save and reopen and on a second document (identical);
- a region smooth with a falloff width (Jacobi against a long-double reference, the neighbours
  outside unchanged, spot points clipped) and a wide smooth over seven tiles (C5, C6, C7);
- the falloff edges on the grid, sampling (dense, reversed and repeated strokes), the refusals
  (each bound, every wrong field combination, an empty brush with its located report, a missing or
  locked terrain, the batch), pads, the pieces (edited by id, skipped with a notice after the sculpt
  is removed, C1 with the skipped entry left out, ids never reused and stable when an earlier
  change is removed), the codec and its refusals, the assistant's request (no `terrain.sculpt`
  tool; the sculpt listed in `terrain.describe`), and a scripted sequence of four sculpts and a
  contour edit with the checks after each, Undo and Redo of all, reopen, Reset edits and a pinned
  replay digest (`81f5fd22…3b688`, the same in Debug and Release builds);
- the review fixes: the hostile comb refused quickly by the command (located), the validator and
  the decoder, a comb at the bound accepted with its inside test equal to brute force, a
  cancellation inside an entry, the bow-tie and collinear regions, a piece from another block
  refused on load and by the hook, and pads off the brush and a degenerate pad changing nothing.

`command_tests` covers the command, and the parser fuzz seeds (a brush sculpt with a recorded
piece, a region smooth, and a rejected smooth of nine iterations) and `parser_fault_corpus_tests`
cover the two codecs. `parser_fuzz_terrain_edits` (libFuzzer, clang with ASan and UBSan, the
documented options, 2026-10-20) ran 349,176 inputs in 301 s from the generated seeds (1,179
coverage points, 587 MB peak) with no failure.

## Implementation notes (R096.d)

Modules: `src/core/terrain_stamp.*` (the kind `stamp`, the daylight and fixed-width solvers, the
self-intersection repair, the cavity, check C3), `src/core/terrain_plan_index.*` (a banded,
tree-indexed plan polygon shared by pads, the daylight polygon and the clipping), the pad index
and pad rays in `src/core/terrain_sources.*`, the general clip `planTerrainSourceClip` in
`src/core/terrain_sculpt.*`, `TerrainMeshView` (R096.c's view of the edit mesh, moved to
`src/core/terrain_mesh_view.hpp`), the codec in `src/io/terrain_io.cpp`, and `terrain.stamp_pad`,
`terrain.update_edit` and the `pads` of `terrain.describe` in `src/automation/terrain_commands.cpp`.
`terrain_stamp.cpp` and `terrain_plan_index.cpp` are compiled with `-ffp-contract=off` like the
other replayed code.

**The kind** (§4.3, §7; [ADR 0108 addendum](0108-public-native-format.md#schema-29-terrain-and-site-origin-r095a)).
An entry holds its parameters in the terrain frame (the footprint, counter-clockwise from its
lexicographically smallest corner; the pad elevation; daylight with cut and fill slopes and a
maximum distance, or a fixed width; the source body and face for Restamp) and its resolved
geometry: the outline as the mesh holds it (the footprint with the edge rays' start points), the
rays in order with their daylight points, the count of dropped rays and the ids of split source
lines. Unlike R096.b's and R096.c's kinds, the resolved geometry depends on the mesh, so it is
recomputed whenever the entry applies: `TerrainEditOutcome` gains the entry's recomputed data,
which both the incremental edit and the replay store (an unchanged result keeps the entry's
pointer, so Regenerate after edits stays a no-op), and the kind's `unresolved` hook clears it
when a replay skips the entry. The mesh-free derivation of the current sources and the pads
then reads exactly what the mesh holds: a skipped pad neither clips nor protects.
`runTerrainRegenerate` stores the replayed entries in the record it returns and refuses a list
that would grow past 8 MiB. Bounds: a footprint of 3 to 20,000 points with at most 512 edges in
a horizontal band (as R096.b's regions; a comb is refused before any work), slopes 0.01 to 100,
a distance above 0 and at most 1,000 m, a width of 0.01 to 1,000 m, at most 32,768 rays.

**Rays.** From each outline vertex: at a convex corner a fan from one edge normal through the
bisector to the other, each half cut into steps of at most 15° by normalized interpolation (no
trigonometry, so no C library's last bit enters replay; the step count bounds the largest step);
a corner turning less than about 0.06° gets one bisector ray, so two rays never leave one point
in almost one direction; at a concave corner one bisector ray (the normals' sum, or the edge
directions' difference where they nearly oppose). Along each edge, rays along its normal at a
spacing no larger than the smallest longest side of the triangles each quarter-cell stretch of the
edge touches (each stretch starts with a ray), and at least 0.01 m. The ray count is checked before
any walk.

**The daylight solver.** A ray is walked through the edit mesh one stored cell at a time: the
triangles of each newly reached tile give the intervals of t in which the ray lies in them
(closed, widened by about 1e-10 m) with the triangle's plane along the ray, processed in
(t0, t1, triangle) order. The ground at the start decides fill (pad above, grade h − fill · t),
cut (pad below, h + cut · t) or touching; the crossing is solved exactly on the first triangle
whose interval ends past it, as h ∓ s · t = g0 + g1 · t. A gap in the intervals before the
crossing is the ray leaving the terrain (refused at the exit point), and no crossing within the
maximum distance is refused at its end (`daylight-distance`). A ray shorter than 0.1 mm meets
the ground at the pad edge: its daylight point is its start at h (the ground there moves by at
most slope × 0.1 mm). Fixed width puts the daylight point at the width, at the ground's height
there. Each step and each triangle tested count against a work bound (2^26), so a pad high above
flat ground with a long maximum distance is refused rather than walked for long.

**Self-intersection repair.** The outline pieces, the rays with length and the daylight line's
edges must not meet except at shared ends (exact predicates; identical segments, a daylight edge
along the outline where the pad meets the ground, are one constraint). Pairs are found through a
uniform grid each segment is walked through (Amanatides–Woo, both neighbours at a corner), with
outline pieces never paired with each other; the work is bounded (2^25). For each crossing, the
rays of the shortest run around the outline that holds the crossing elements are dropped (for a
ray crossing the outline, that ray), and the check repeats, at most 64 rounds. The daylight
points left keep the outer boundary; the report gives the count and the first dropped point
(`daylight-repaired`). A daylight point within 1e-6 m of the one before it is dropped too (a fan
over a very short reach would only add slivers). The daylight polygon must then run
counter-clockwise around the footprint.

**The mesh result.** The daylight polygon widened by 1e-6 m (`terrainStampSlack`) decides what
the pad replaces: a flood from the footprint collects every triangle meeting it (memoised
point classification, the polygon's tree for edges), and every vertex in it is removed, so the
pad interior is a flat CDT of the outline at exactly h and no kept vertex lies within 1e-6 m of a
new one. An outline or hole edge the polygon reaches, or a removed vertex on one, is the pad
leaving the terrain (`outside-outline`). Each inserted vertex adds its Bowyer–Watson cavity, as
in R096.c, never across a kept constraint, the outline or a pad; the cavity is bounded by
R096.c's 65,536 triangles. `retriangulateTerrainCavity` gets the kept vertices, the cavity
boundary, the kept constraints (current source segments the widened polygon does not meet), the
outline as a closed breakline at h, every ray as a two-point breakline and the daylight line as a
closed breakline; a new point the adapter would merge into another is refused, so the stored
geometry is exactly the mesh's (C1, C3). Pad vertices are breakline vertices, not contour
vertices, so the flat-triangle rule leaves the pad alone.

**Current sources.** The `sources` hook clips every contour, breakline and spot-point vertex in
the widened polygon and cuts every line segment that meets it (R096.b's clip, generalised as
`planTerrainSourceClip`: a rule marks vertices and, now, segments between kept vertices, and a
ring opens at its first break), then adds the outline and the daylight line as closed breaklines
with the first two ids of the entry's block (`terrainSplitSourceId` ordinals 0 and 1); further
runs of split lines take recorded ids first, then ordinals from 2. The rays are not sources, but
they are constraint segments of the mesh: the state counts them in `segmentUses`, so later
cavities (stamps and contour edits) keep them and Bowyer–Watson never crosses them; a newer pad
whose widened polygon meets a ray drops it from the state (`dropPadRays`), as its cavity drops it
from the mesh.

**Overlaps and protection** (decision 6). `protects` gives the footprint and the new hook
`protectedPoints` the outline's vertices, which lie on the footprint's edges only up to
rounding; a protected region keeps its polygon index (bands and a box tree) and its points, and
`TerrainPadIndex` finds regions by a box tree, so sculpt, smooth, contour edits and stamps test a
point or segment against a few pad edges rather than every pad. R096.b's and R096.c's pad tests
(`Pads`, `checkProtected` and the Bowyer–Watson guard) now use it. A stamp whose widened daylight
polygon meets another pad's footprint, outline points or edges is refused (`inside-pad`, located,
naming the other change); one that meets only the other pad's slopes applies, and the newer slope
wins there. Removing a pad's entry lifts its protection (tested: the replayed sculpt moves the
area).

**Check C3** (`checkTerrainPadConsistency`). For every applied pad: every mesh vertex on or
inside its footprint, or at an outline vertex, has Z bit-equal to h; every outline edge is a mesh
edge; and along every unsuperseded ray with length, the ray is a mesh edge, the daylight point a
mesh vertex with its stored Z, and |h − z| / run equals the fill slope (z below h) or the cut
slope within 1e-9 relative (daylight), or the run equals the width within 1e-9 relative. A ray is
superseded by a later applied pad whose widened polygon meets it (the rule that drops it), or by
any later entry of another kind whose plan box meets the ray's (a choice where the ADR is open: a
sculpt may move a daylight point, a contour edit may re-triangulate next to a ray). The tests add
`terrain.elevation_at` inside each pad (the pad elevation within 1e-9 m).

**Commands** (§8). `terrain.stamp_pad {terrain, footprint: {body, face} | {body} | {edges} |
{polygon}, elevation?, mode?, cutSlope?, fillSlope?, maxDistance?, width?}` ("Stamp pad"): a
horizontal face without holes; the lowest such face of a body or of the geometry in a group or
component (ties to the smaller IDs); one closed loop of model edges, chained as
`terrain.create_from_edges` does; or model points. The footprint is projected to plan in the
terrain frame, cleaned and made canonical; its own height (when every point has one within 1e-6
m) is the default elevation. Defaults: daylight, slopes 0.5 (1:2), 100 m. The terrain must be
level and unscaled, as for sculpts. The command's dry run gives the located refusal report; its
outcome is then committed as is (`applyTerrainEdit` takes a planned outcome), so a stamp is
planned once. The report (`format: "terrain-edit"`) counts the rays (cut, fill, touching,
dropped), the cavity, the tiles and the clipped sources, with notices `sources-clipped`,
`daylight-repaired`, `face-paint-dropped` and `flat-triangles`, and the result carries the pad
(elevation, outline and extent boxes in model coordinates).
`terrain.update_edit {terrain, edit, restamp?, elevation?, mode?, cutSlope?, fillSlope?,
maxDistance?, width?}` ("Edit terrain change") replaces a pad's entry, keeping its ID and place,
by its new parameters or, with `restamp`, its footprint re-read from its source body (the source
face when it is still there and horizontal, else the body's lowest face), and replays the list
through R096.a's replay request (progress and cancel included). The changed entry must apply:
`runTerrainRegenerate` takes the required entry and refuses the run with that entry's located
problems; later entries that no longer apply are skipped with notices as for Remove change. A
change that is not a pad, a pad from edges or a polygon (no source body), a missing source body,
and an update naming nothing are refused. `terrain.describe` adds `pads {count, space: "model",
entries: [{edit, elevation, mode, slopes or width, outline, extent, rays, raysDropped,
sourceBody?, applied}] (at most 200), entriesOmitted}`. The in-app assistant gets no tool
(decision 9): `assistantVisible` hides `terrain.` commands, and `assistantResult` drops `pads`
from `terrain.describe` as it drops `currentSources`, so the assistant's schemas,
`assistant_tool_surface_tests` and corpus v5 are unchanged.

**Choices where the ADR is open.** (1) `terrain.update_edit` always replays from the base, also
for the last entry, as Remove change does: the mesh before an entry is not kept, and replay
equals incremental. The cheap revision right after placement is the desktop's amend (§4.3,
R096.f); `Document::amendLast` refuses a replacement that touches other tiles than the original
edit, so R096.f needs a terrain-aware amend. (2) Restamp keeps the stored elevation unless one is
given (the pad offset is a desktop setting). (3) The entry remembers the source face as well as
the body, so Restamp of a face footprint re-reads that face. (4) Rays are constraint segments
but not sources (sources would need an id per ray). (5) The 0.1 mm touch rule, the 1e-6 m slack
and the work bounds above.

**Measured** (`terrain_edit_benchmark --edits 1 --replay 0 --cancel 0 --source-edits 0
--sculpts 0 --stamps 100 --stamp-replay 40`, Release, GCC, the development host with 22 CPUs).
The two terrains are those of
`terrain_budget_benchmark` built from 2 % fewer points or rings (244,970 and 244,498 triangles,
256 and 3,108 tiles): stamps add triangles (a one-tile pad about 100 to 150), and at the
document's 250,000-triangle limit every stamp is refused with `LIMIT_EXCEEDED` naming the count,
which the first run showed. A one-tile sample is a square pad a quarter of a tile across in the
middle of one of the 64 largest tiles, 0.2 m above the ground there; samples whose touched tiles
hold more than 2,048 triangles (2 of 100 on each terrain) are left out of the budget rows. The
contour terrain's ground falls 0.55 a metre, steeper than the default 1:2 fill, so its pads grade
at 1:1. Display = the per-tile surface and contour caches, without GPU upload or drawing.

| Terrain | One-tile pad: rays, cavity triangles (p95) | Preview p95 (max) | Commit p95: core / command / command + display (max) | Larger pad (a tile across, 9 tiles): preview / command p95 | 64-vertex footprint: preview / command p95 | Update of the first pad (replay) / replaying Regenerate |
| --- | --- | --- | --- | --- | --- | --- |
| 122,500 points (244,970 triangles) | 96; 191 | 3.8 (13.2) ms | 46.0 / 45.7 / 56.3 ms (58.4) | 12.9 / 101.2 ms | 7.9 / 53.2 ms | 0.72 s / 0.70 s (15 pads) |
| 408 contour rings (244,498 triangles) | 72; 274 | 3.8 (30.6) ms | 57.3 / 58.2 / 75.7 ms (79.3) | 12.3 / 91.8 ms | 9.1 / 67.4 ms | 0.88 s / 0.88 s (27 pads) |

A one-tile pad commits within decision 10's 100 ms at p95: 46–58 ms through the command and
56–76 ms with the display update (re-measured after the review fixes below). As for the other kinds, most of it is the whole-terrain digest;
the plan (rays, walk, repair, cavity and adapter) is the preview's about 4 ms, and the command
plans once. A pad a whole tile across touches nine tiles and commits in 92–101 ms p95, reported as
the nine-tile sculpt is. The 64-vertex footprint of §9 previews in at most 9.1 ms. Changing an
earlier pad replays the list (0.7–0.9 s with the base CDT); a replaying Regenerate of the pads is a
no-op and C1, C3 and C6 hold after it. Hostile inputs: a 20,000-point circle a third of a tile
across is refused (its pad would pass the triangle limit) in 0.24 s and 2.3 s, and a comb of 8,000
corners is refused by the band bound in 2 ms. An earlier run on the same host while the machine
was busy measured the contour terrain's command and display at 108 ms p95; the figures above are
a repeat on the quiet machine.

**Verification.** `terrain_stamp_tests` (ctest `terrain_stamp`, dev and sanitize presets) on the
40-ring contour hill, the 12,000-point terrain and gridded point terrains: square pads on the hill (the preview equals the commit; C1, C2, C3, C5, C6
and C7; Undo, Redo, save and reopen; `terrain.describe`'s pad); an L-shaped pad on the point
terrain with cut and fill and the concave corner's bisector (and the same stamp on a second
document, identical); a pad over a tile corner of a 120 × 120 m grid (four tiles replaced) and a
fixed-width pad (every run equals the width); the footprint forms (a face at its own height, an
edge loop, a group's lowest face, remembered for Restamp); protection (a raise and a smooth over a
pad leave it, with `verticesProtected`; a breakline across it, a spot point on it and removing its
outline source are refused as `inside-pad` and change nothing; removing the pad lifts it);
overlaps (a pad whose slopes enter another refused with its location, one entering only the
slopes applied, the older pad's rays superseded); refusals (a ray leaving the terrain, no
daylight within the distance, off the terrain, a self-crossing and a pinched footprint, each
bound and field rule, a missing body or terrain, 20,001 points, a locked terrain), each changing
nothing; `terrain.update_edit` (elevation, slopes, mode and Restamp of a non-last pad, each one
"Edit terrain change" with C1, C2, C3, C5, C6 and C7, Undo, Redo and reopen; a sculpt, a missing
change, an empty update, slopes for a fixed-width pad, an update whose rays find no daylight with
its located refusal, a polygon pad and a lost source body refused); the repair (a 4 m notch: 40 of
140 rays dropped, reported and located); the codec and its refusals; the assistant (no stamp tool,
no `pads`); a cancellation inside an entry; hostile inputs (a 20,000-point circle, a comb, 4,000
sharp teeth past the ray bound); and a scripted sequence of two pads, a sculpt and an update with
the checks after each, Undo and Redo of all, reopen, Remove change and Reset edits, its replay
digest pinned (`04bc036f…98ffc2`, the same in Debug and Release builds). `command_tests` covers
the two commands, and the parser fuzz seeds (a resolved daylight pad, an unresolved fixed-width
pad, and a rejected pad whose rays run backwards) and `parser_fault_corpus_tests` cover the codec.
The R096.b and R096.c suites pass unchanged, with their pinned digests, on the shared pad index
and clip. `parser_fuzz_terrain_edits` (libFuzzer, clang 23 with ASan and UBSan, the documented
options, 2026-10-20), now with the pads in its seeds, ran 259,620 inputs in 301 s (1,679 coverage
points, 622 MB peak) with no failure.

**Review fixes.** An internal review before the pull request found no partial mutation on a
refusal; these follow it. Bowyer–Watson walks count every visited triangle against R096.c's
touched-triangle bound. A polygon index halves its bands while they would list more than 32 entries
a corner (a zigzag daylight line listed each edge in thousands of bands). The edge-spacing pass
counts the triangles it scans against the walk's bound. The crossing search sizes its cell so the
walks list about five cells a segment, counts its pairs before testing any (so a refusal does not
depend on hash order), tests them cell by cell instead of collecting them, and allows a shared
corner only between neighbouring footprint edges (a footprint pinched at one corner is refused).
`dropPadRays` skips pads whose box misses, the other-pads check tests only points and edges in each
pad's box, the repair runs at most 16 rounds, and C3's grade test allows the rounding of the stored
daylight point.

**Review fixes (PR #389).** An independent review found no blocker; these follow it, each with a
regression test in `terrain_stamp_tests`.
- *A pad's own sources.* The outline and daylight line (the entry's block ordinals 0 and 1) are
  marked in the derived state, and `terrain.edit_source` or `terrain.remove_source` naming one is
  refused (`pad-source`, located, pointing to `terrain.update_edit` and `terrain.remove_edit`); a
  stored entry that tries does not apply (it is skipped on replay). The rays' ends also count as
  vertex uses, so no source edit removes them. Removing or lifting the daylight line would
  otherwise have removed the slopes while C3's box rule counted every ray superseded; with the
  refusal no edit of the pad's own sources can supersede its rays (`pad sources`).
- *Hostile files.* A resolved pad's daylight line, like its footprint, may cross a horizontal band
  at most 512 times: the validator and the decoder refuse more, and the solver refuses such a line
  (corrugated ground across the rays) with its band located, so the mesh-free derivation of the
  current sources reads at most a band of edges a vertex. A crafted pad with a 32,768-point zigzag
  line is refused on load in 3 ms (`hostile ring`).
- *The triangle limit.* A stamp or contour edit that would pass the document's 250,000 terrain
  triangles is refused before the commit (`triangle-budget`, `LIMIT_EXCEEDED`, located at the
  change), naming the count and what the change needs, with the remedies (`triangle limit`).
- *Cancellation.* The crossing search checks for cancellation once per grid cell; a cancellation at
  each of a plan's checkpoints stops it there (`cancel everywhere`).
- *Local ray spacing.* The spacing along an edge is chosen per stretch a quarter cell long (the
  smallest longest side of the triangles that stretch touches; each stretch starts with a ray),
  so a sliver makes rays dense only where it is: a 30 m edge past a 4 cm cluster of points gets 146
  starts rather than about 750 (`local spacing`).

**For R096.f (tracker).** Typing a new elevation or slope right after a stamp must amend that
stamp (UX §15.4). `Document::amendLast` refuses a replacement that changes other bodies than the
original edit or creates another number of them, which a re-stamp usually does (other tiles), so
R096.f needs a terrain-aware amend: undo the stamp's own edit and re-run `terrain.stamp_pad` on
the state before it as one replacement, allowed to replace a different set of the same terrain's
tiles. Until then the desktop can revise the last pad through `terrain.update_edit`, which replays
the list.

## Implementation notes (R096.e)

Modules: `src/core/terrain_drape.*` (the kind `drape`, the plan imprint and overlay share, the
overlay geometry, the paint regions), the paint hooks in `applyTerrainTiles`
(`src/core/terrain_edit.cpp`) and `regenerateTerrain` (`src/core/terrain.cpp`), the codec in
`src/io/terrain_io.cpp`, and `terrain.drape` and the `paintRegions` of `terrain.describe` in
`src/automation/terrain_commands.cpp`. `terrain_drape.cpp` is compiled with `-ffp-contract=off`
like the other replayed code.

**The kind** (§4.4, §7; [ADR 0108 addendum](0108-public-native-format.md#schema-29-terrain-and-site-origin-r095a)).
Only imprints are recorded. An entry holds its parameters in the terrain frame, the lines (edge
chains and face loops projected to plan, open or closed) and the paint regions (a face's loops and
its material), and its resolved geometry: the runs as the mesh holds them, with Z, and the
existing constraint segments its new vertices split, with those vertices. As for a pad, the
resolved geometry depends on the mesh, so it is recomputed whenever the entry applies and cleared
when a replay skips it (`unresolved`). Bounds: 1 to 4,096 lines and 20,000 points, 1,024 paint
regions of 20,000 points with at most 512 edges in a horizontal band, 4,096 runs and 65,536 run
points, 65,536 split points.

**Projection and splitting.** Projection is vertical in the terrain frame; the lines' own Z is not
used. The plan runs in three steps, each a pure function of the mesh and the entry:
- *The lines meet only at shared points.* Input points within 1e-7 m of each other become one
  point (the lexicographically smallest of each cluster), a point within 1e-7 m of another line's
  segment is inserted into it, and two segments that cross take their crossing point, computed
  from the two segments in a canonical order (each from its smaller end), so a face loop and its
  own edges, or two faces sharing an edge, give the same segments bit for bit. Pairs are found
  through a grid of cells about one mean segment long; the pairs are counted before any is tested
  (at most 2^25) and the crossings are bounded (65,536).
- *Each segment is split against the mesh.* The candidate triangles are those the closed segment
  touches (R096.c's grid walk). Mesh vertices within 1e-7 m of the segment bend it through them
  (the "snap"); between two stops, every mesh edge the piece crosses properly (exact orientation)
  gives a crossing point, computed from the edge's smaller end and the piece's smaller end, so a
  segment and its reverse agree. A point within 1e-9 m of an edge (and not near a vertex) is moved
  onto that edge. Points on one edge within 1e-7 m of each other are one point (the first found),
  so two lines crossing an edge almost together share a vertex. Z: a vertex keeps its own; a point
  on an edge takes the edge's elevation at its parameter from the smaller end (both triangles
  agree, and a contour edge, whose ends share one elevation, gives exactly that elevation); a point
  inside a triangle takes the triangle's plane by barycentric interpolation from its smallest
  corner. Each piece between consecutive points lies in one triangle, runs along a mesh edge, or
  lies outside the outline or in a hole (no common triangle and a midpoint on no triangle): those
  gaps are clipped, counted and located, and the line continues as a new run where it re-enters.
  A run along a mesh edge passes every new vertex on that edge, so every run segment is a mesh
  edge.
- *Imprint splits the triangles that hold a new vertex*, each within itself: the triangle's
  corners with the new vertices on its edges form its boundary, the new vertices inside it are
  kept vertices, and the pieces inside it are constraints, re-triangulated by
  `retriangulateTerrainCavity` with no flat-triangle flip. Every new triangle lies in the plane of
  the triangle it came from, so the surface's shape does not change (C4). A new triangle below the
  modelling tolerance (its cross product under 1e-12 m², as R096.c) is refused with its location
  (`degenerate-triangle`), and so is a piece the exact split cannot place (`triangulation-failed`,
  "move it slightly"). Nothing outside the split triangles changes.

**Current sources.** The `sources` hook reads no mesh: each split segment's points are inserted into
the current contours and breaklines that hold it (a contour's crossing points take its exact
elevation), a pad ray it split is superseded (`dropPadRays`, as a newer pad's would be), and each run
joins as a breakline with id ordinal n of the entry's block (`terrainSplitSourceId`, 4,096 a
change). So C1 holds: every current contour and breakline vertex is a mesh vertex and every segment a
mesh edge, the drape's lines included.

**Protections** (decision 6; the R096.d lesson). Choices where the ADR is open:
- *Pads.* A run that meets a pad's footprint or its outline (closed, through R096.d's pad index, its
  protected points included) is refused (`inside-pad`, located, naming the change). A run may cross
  a pad's slopes, as another pad's slopes may: the crossed rays are superseded, which C3's box rule
  already counts, and the daylight line gains the crossing point like any breakline.
- *The drape's own lines.* They are the entry's resolved geometry, so, like a pad's outline and
  daylight line, `terrain.edit_source` and `terrain.remove_source` naming one are refused
  (`drape-source`, located, pointing to `terrain.remove_edit`); the runs a later sculpt or stamp
  splits off one stay the drape's own. A later drape that crosses an earlier drape's line inserts the
  crossing point into it, as it does into a contour.
- *Contours a drape crossed* now share the crossing vertices with the drape's lines, so re-elevating
  such a contour (`terrain.edit_source` with only `elevation`) is refused as R096.c refuses moving a
  shared vertex; moving or removing the contour keeps the crossing vertices (another source uses
  them).
- *The outline.* Where a line leaves the terrain the run ends at the outline: the boundary edge gains
  the crossing vertex (the outline's plan is unchanged up to rounding). Boundary and hole sources are
  not split (they are not constraint segments of the current sources).

**Region paint** (decision 7). With `paintRegions`, each draped face with a material (its front
material, else its back) records its loops and that material; faces without one are imprinted and
counted (`paint-without-material`). `TerrainPaintRegions` holds the regions of the applied drapes in
list order, each loop indexed by R096.b's horizontal bands, and a triangle takes the material of the
last region holding its centroid (even-odd over the region's loops), on both sides. Where:
- every tile an edit builds is painted (`applyTerrainTiles`), and when the record gains regions (an
  imprint with paint, appended last) the faces of kept tiles inside them are repainted: those tiles
  keep their mesh and ID and are listed as `repainted` (C5's pointer identity holds for every tile
  neither replaced nor repainted);
- every tile Regenerate, Terrain settings, Remove change and Reset edits build is painted
  (`regenerateTerrain`), so a tile whose mesh and regions did not change compares equal and stays
  (C7 holds with paint); removing the drape removes its paint;
- paint the regions give a face again is not counted as dropped (`face-paint-dropped`,
  `terrainTilePaint`).
Material validation keeps a region's material alive: `TerrainEditData::materials()` names it, and
deleting a material a region names is refused like deleting one a face uses. A crafted file cannot
make painting slow: a region's loops are refused past 512 edges in a band, and the loops of all
regions of a terrain past 4,096 edges in their busiest bands, so a centroid test reads at most that.

*A limit of centroids.* Paint and overlay decide by a triangle's centroid against the face's own
loops, while the imprinted boundary may sit up to 1e-7 m away (the vertex snap). A piece narrower
than about 3e-7 m (yet above the sliver tolerance) can therefore land on the other side; that is
far below anything visible.

**Overlay** (§4.4). The same plan with `overlay` skips the pad rule and the id block. Its runs from
`edges` become loose edges; each face becomes one planar polygon per terrain-triangle piece inside
it: the (sub)triangles whose centroid is in the face (a later face wins where faces overlap), merged
per terrain triangle by cancelling shared edges into an outer loop and holes (a pinched piece falls
back to its triangles). Everything is lifted by `lift` (default 0.01 m, at most 1,000 m, model metres
up) and goes into a new group "Drape" in `context` (the active context: a group, or the model),
holding one geometry body "Draped geometry" in the terrain's frame; pieces keep their face's
materials. The terrain record and its tiles do not change. The faces count against the 100,000-face
modelling limit: the overlay is refused before anything is made when it would pass the room left
(`LIMIT_EXCEEDED`, with the document's count), and classifying is bounded (2^22 triangles).
Choices where the ADR is open: the `context` field (the ADR's command has none; the desktop passes
its active context), the names, and that a component context is refused (overlay adds to a group or
the model).

**Command** (§8). `terrain.drape {terrain, edges?, faces?, mode: imprint|overlay, paintRegions?,
lift?, context?}` ("Drape on terrain"), one edit through the batch API with its dry run, MCP and
sessions. `edges` are chained as `terrain.create_from_edges` chains them (so edges of a locked body
are refused there too, and so, to match, are faces of a locked body); `faces` give every loop, in
any context, read in world coordinates. `mode`
is required (decision 5's default is the desktop's). `paintRegions` is imprint only; `lift` and
`context` are overlay only. Vertical edges and faces (no plan length or area) are dropped with
notices; nothing left is refused. Imprint needs an unlocked, level terrain; overlay a level one (it
reads a locked terrain). The command's dry run gives the located refusal report, the triangle limit
is checked before the commit (`triangle-budget`, located, as R096.d), and the outcome is committed as
planned. The report (`format: "terrain-edit"`) counts the lines, runs, points, crossings, snapped
vertices, inserted vertices, triangles split and added, sources split, clipped parts (with a
`drape-clipped` notice located at the first) and, for imprint, the tiles replaced, added and
repainted and the paint regions; overlay returns the group, the body and its face and edge counts.
`terrain.describe` adds `paintRegions {count, space: "model", entries: [{edit, material, loops,
extent, applied}] (at most 200), entriesOmitted}`. The in-app assistant gets no tool (decision 9):
`assistantVisible` hides `terrain.` commands, and `assistantResult` drops `paintRegions` from
`terrain.describe` as it drops `currentSources` and `pads`, so the assistant's schemas,
`assistant_tool_surface_tests` and corpus v5 are unchanged.

**Work bounds and cancellation.** Besides the counts above: the triangles segments find (16 ×
65,536, as R096.c), the triangles and edges scanned while locating points and splitting segments
(2^27; each piece between two snapped vertices tests only the edges whose projection on the segment
meets it), the triangles split (65,536), and the points clustered (2^25 comparisons). The plan checks
for cancellation per line, every 4,096 pair tests and every 64 split triangles, so a replay is
cancelled part-way through a long drape (tested at every checkpoint).

**Measured** (`terrain_edit_benchmark --edits 1 --replay 0 --cancel 0 --source-edits 0 --sculpts 0
--sculpt-replay 0 --stamps 0 --stamp-replay 0 --drapes 100 --drape-replay 100`, Release, GCC, the
development host with 22 CPUs). As for pads, drapes add triangles (a one-tile drape about 70), so
they are measured on the two terrains of `terrain_budget_benchmark` built from 2 % fewer points or
rings (244,970 and 244,498 triangles, 256 and 3,108 tiles); at the document's 250,000-triangle
limit every drape is refused with the located `triangle-budget` report. A one-tile sample is a
three-point line across the middle of one of the 64 largest tiles, 60 % of its smaller side;
samples whose touched tiles hold more than 2,048 triangles (4 of 100 on the point terrain) are
left out of the budget rows. Display = the per-tile surface and contour caches, without GPU upload
or drawing.

| Terrain | One-tile drape: triangles split, touched triangles (p95) | Preview p95 (max) | Commit p95: core / command / command + display (max) | Long line across the terrain: length, tiles, triangles split; preview / command / display | Painted face (8 tiles): command; then Regenerate | Overlay of a one-tile face | Replaying Regenerate (entries) |
| --- | --- | --- | --- | --- | --- | --- | ---: |
| 122,500 points (244,970 triangles) | 41; 1,159 | 0.8 (9.5) ms | 40.9 / 41.4 / 50.6 ms (51.3) | 1,115 m, 25 tiles, 836; 11.9 / 211.9 / 62.1 ms | 103.9 ms; 0.62 s | 432 faces, 4.3 ms | 0.64 s (90) |
| 408 contour rings (244,498 triangles) | 41; 926 | 1.3 (22.1) ms | 55.1 / 55.7 / 72.4 ms (73.5) | 1,011 m, 98 tiles, 1,781; 25.2 / 182.7 / 61.2 ms | 116.4 ms; 0.77 s | 470 faces, 5.6 ms | 0.82 s (100) |

A one-tile drape commits within decision 10's 100 ms at p95: 41–56 ms through the command and
51–72 ms with the display update; as for the other kinds, most of it is the whole-terrain digest,
and the plan (the arrangement, the split and the per-triangle re-triangulation) is the preview's
1 ms. A line a kilometre long across 25 to 98 tiles previews in 12–25 ms and commits in 183–212 ms
(rebuilding and validating every tile it crosses), reported as the nine-tile sculpt is. A replaying
Regenerate of 90–100 drapes (short lines, placed while the terrains stayed 3,000 triangles under the
limit) takes 0.64–0.82 s with the base CDT, and is a no-op; C1 and C6 hold after it and after the
long line. Hostile inputs (`terrain_drape_tests`, Debug): a 300 × 300 grid of lines (90,000
crossings among themselves) is refused by the crossing bound in 0.2 s, a 10,000-segment zigzag
across the terrain by the touched-triangle bound in 0.1 s, 20,000 points within 2 micrometres
(one point) in 0.2 s, and a serpentine of 1,170 rows through 120 grid vertices each by the point
bound in 3.1 s (the split stops once its parts pass twice the run-point bound); a crafted record whose split segment one source passes 10,000 times derives in
15 ms with the drape's effect left out.

**Verification.** `terrain_drape_tests` (ctest `terrain_drape`, dev and sanitize presets) on the
40-ring contour hill, the R095 contour fixture (boundary and pond), the 12,000-point terrain and
gridded point terrains:
- *path*: a three-segment line across the hill (246 mesh-edge crossings, 33 contour segments split,
  seven tiles): the preview equals the commit; C4 at 10,000 random points in the line's box (worst
  2.8e-14 m); each run is a current breakline and the drape's own, and every run point on a contour
  carries its exact elevation; C1, C2, C5, C6 and C7, also after Undo, Redo, save and reopen; the
  same drape on a second document is identical; `terrain.remove_source` of the draped line is
  refused (`drape-source`), and `terrain.describe` lists the drape;
- *clipping*: a line from outside the boundary across the hill, the breakline and the pond: three
  parts clipped (located, `drape-clipped`), two runs, the breakline gains the crossing point, C4,
  C1, C2, C6, C7, history and save; a line wholly outside or in the pond is refused
  (`outside-outline`) and changes nothing;
- *paint*: a face with a hole, in a group, with a material, imprinted with region paint over most
  of the 12,000-point terrain: every triangle is painted exactly when its centroid is in the region
  (13,757 of 25,575), tiles inside but not crossed are repainted with their IDs and meshes, every
  other tile is pointer-identical; C4, C1, C6, C7, history and save; the paint survives a later
  sculpt across it and Regenerate (no paint counted as dropped); the material cannot be deleted
  while the region names it; removing the drape removes its paint;
- *overlay*: a face and a line in a group context, lifted 0.05 m: the terrain record and tiles are
  unchanged, the geometry is a new group in the context, every vertex lies at the lift above the
  surface (within 1e-9 m), every face is planar, the faces cover the face's area exactly, the
  modelling face count grows by the overlay's faces, Undo removes it, and the overlay's face bound
  refuses;
- *grid*: on a 1 m grid, a line along a grid row, a diagonal through grid vertices and a crossing
  line, over an added breakline (three crossings with each other, 56 vertices snapped, the
  breakline gains three points): C4, C1, C6, C7, history and save; draping the same lines again
  splits nothing;
- *pads*: a line across a pad and one along its outline edge are refused (`inside-pad`) and change
  nothing; one across its slopes applies with C3, C4, C1, C6 and C7;
- *sliver*: a line 2e-7 m from a row of grid vertices is refused (`degenerate-triangle`, located);
  at 5e-8 m it snaps through them, with C4, C1, C6 and C7;
- *refusals* (each directly and through the batch, changing nothing): no mode, an unknown mode,
  nothing to drape, paint with overlay, lift or context with imprint, a negative lift, an unknown
  field, a missing edge or face, a tile's face, a context that is not a group, a missing terrain, a
  vertical face, a locked terrain (overlay reads it), 20,005 points;
- the codec round trip and its refusals (an unknown field, no lines, a one-point line, `closed`
  written false, splits without runs, a numeric material, a split with its larger end first, a
  one-point run, a comb paint loop); the assistant (no `terrain.drape` tool, no `paintRegions`,
  the drape read in its edits); a cancellation at each of the plan's checkpoints; the hostile inputs
  below; the triangle limit (refused, located, naming the counts); and a scripted sequence of a
  painted drape, a sculpt across it, a pad and a second drape crossing the first, with the checks
  after each, Undo and Redo of all, reopen, Remove change and Reset edits, its replay digest pinned
  (`11e74534…22ddcb1a`, the same in Debug and Release builds);
- the review fixes below: a crafted record whose split segment one source passes 10,000 times
  (its effect left out, quickly), and a stored source edit naming a drape's line (left out of the
  derivation).

`command_tests` covers the command, and the parser fuzz seeds (a resolved drape with a paint
region and a split, an unresolved one, and a rejected one whose split runs backwards) and
`parser_fault_corpus_tests` cover the codec. The R096.b, R096.c and R096.d suites pass unchanged,
with their pinned digests. `parser_fuzz_terrain_edits` (libFuzzer, clang 23 with ASan and UBSan,
the documented options, 2026-10-20), now with the drapes in its seeds, ran 474,260 inputs in 301 s
(2,352 coverage points, 616 MB peak) with no failure.

**Review fixes.** An internal review before the pull request found no partial mutation on a
refusal and no path breaking C1 or C4; these follow it.
- *Source growth.* A split's points are inserted wherever a current source passes its segment, so a
  crafted entry whose segment many sources share (or one source passes many times) could have grown
  the derived sources without bound. The plan and the `sources` hook count the growth alike (the
  runs plus each split once per use) and a drape that would take the current sources past
  2,000,000 vertices is refused (`LIMIT_EXCEEDED`); a stored one changes nothing.
- *Protected sources in the derivation.* A stored `editSource` or `removeSource` naming a pad's
  outline or daylight line, or a drape's line, was skipped by the replay but still applied by the
  mesh-free derivation; it is now left out there too (R096.d's pad rule included).
- *Bounded scans.* The hook skips source segments outside the splits' box before any lookup; paint
  tests only the regions whose boxes meet a tile's, also when counting paint a rebuild drops;
  overlay counts every piece of a split triangle against its work bound and indexes each outer loop
  once; each piece between two snapped vertices tests only the edges beside it.
- *Decoder.* A paint loop of fewer than three points is refused as it is read, so empty loops cannot
  be many; a run that repeats a point is refused.

**Review fixes (PR #390).** An independent review found no blocker; these follow it, each with a
regression test in `terrain_drape_tests`.
- *Region paint in history.* Measured (`terrain_edit_benchmark`, Release, a painted face over a whole
  terrain, its box inset 3 %, on terrains built 4 % smaller so its outline fits under the limit:
  245,579 and 240,666 triangles): the imprint replaces 56 and 104 tiles and repaints 169 and 2,843,
  painting 214,718 and 235,770 faces; it commits in 1.13 and 0.80 s, and a replaying Regenerate after
  it is a no-op in 0.68 and 0.81 s. Its history charge was 224 and 247 MB, because a repainted tile
  was charged as any changed body, its before and after copies in full. A tile whose paint alone
  changes is now charged one copy of its mesh plus the larger of its two paints (`model.cpp`,
  `paintOnlyTileChange`): the history holds one copy of it at a time (the before copy while the edit
  is done, the after copy while it is undone), so this is still a bound on what the history holds.
  The charge falls to 148 and 136 MB (`paint history`: a fully painted tile is charged 0.6 of a
  rebuilt one). Resident memory grew by 243 and 151 MB in the same commits, so charging only the
  paint delta (about 20 MB) would have under-counted the memory the history keeps alive; the tiles'
  meshes cannot be shared between the two copies without changing `Body`, which stores them by value.
  The ≥ 64 one-tile undo entries of decision 10 still hold: the history drops its oldest entries
  first, and even the whole-terrain paint leaves room for over 360 one-tile edits.
- *Paint work.* Every paint pass (the tiles one edit builds and repaints, or a Regenerate's) charges
  each region it tests and the band edges it reads against 2^27 and refuses past it
  (`limit-exceeded`, located); a Regenerate's paint now runs on the replay (`paintTerrainTileSet`,
  the run a desktop worker cancels) with a cancellation checkpoint every 4,096 tests, and applying
  the tiles only copies its result. A crafted record of 640 thin frames around a 60,000-triangle
  terrain is refused by Regenerate, located, in 7.1 s (Debug), and a cancellation stops the pass
  (`paint work`).
- *Crossed contours.* Re-elevating a contour or breakline whose vertex a drape's line (or a pad's
  outline or daylight line) shares is refused as `crossed-by-change`, located at the shared vertex,
  naming the change ("This contour is crossed by Drape (change #4); remove that change with
  terrain.remove_edit to re-elevate it alone."); other shared vertices keep the generic conflict.
  Removing such a contour is allowed and leaves the crossing points, vertices of the drape's lines,
  where the drape put them (`crossed contour`; UX_DESIGN §15.3 and §15.5).
- *Locked bodies.* Faces of a locked body are refused like its edges (`refusals`).
- §8's command table names `context`.

## Implementation notes (R096.f1)

R096.f is split as the Implementation split allows. **f1** (this section) delivers the terrain
preview path, the Sculpt and Smooth tools, the Terrain menu, the rail's Terrain slot, the context
menu entries and the tool options strip. **f2** delivers Edit contours, Stamp pad, Drape and the
Terrain edits list.

Modules:
- `src/app/viewport_sculpt.cpp`: the tools, the preview display and the overlay.
- `src/app/terrain_tool_strip.*`: the strip.
- `src/app/terrain_tool.hpp`: the options.
- `src/app/window_terrain.cpp`: the menu, the rail slot and the context menu.
- `src/app/flow_layout.hpp`: the wrapping layout, shared with the component Preview strip.
- `terrainPreviewDisplay` and `terrainTileContours` in `src/core/terrain_display.*`.
- `previewTerrainSculpt` and the remembered plan in `src/core/terrain_sculpt.*`.
- `AmendPolicy::TerrainTiles` in `src/core/model.*`.

**The terrain preview path (§5).**
1. **Planning.** Every pointer move of a terrain tool plans the change on the cached edit mesh and
   current sources (`previewTerrainSculpt`). It runs on the UI thread, as §5 allows for sculpt.
   - A hover plans only and counts the tiles with a moving vertex. That is enough for the dots, the
     label and the empty ring.
   - A stroke, an anchored spot or a closed region also builds the candidate tiles through
     `previewTerrainEdit`. That function now takes the planned outcome, as `applyTerrainEdit`
     does.
2. **What is drawn.** `terrainPreviewDisplay(mesh, region)` returns the tiles to draw in place of
   the originals: the candidate tiles and the **seam ring**. The seam ring is the kept tiles that
   hold a position of a replaced tile or a candidate tile.
   - **Normals.** At each exact position, a drawn tile's normal sums the weighted normals of the
     candidate terrain's triangles in (tile key, index) order. That is the order
     `TerrainSurfaceCache` uses.
   - **Outer edges** are the edges that only one triangle of the candidate terrain uses.
   - **Drawing.** The viewport hides the bodies of the replaced and seam-ring tiles in the opaque
     and edge passes, and draws one batch of the candidate surface and its outer edges instead.
     The colour is the tile's material and colour, as the commit gives a rebuilt tile, or
     elevation bands over the candidate's range.
3. **Contours.** `TerrainContours` now records each tile's first segment (`tileStarts`), and the
   viewport keeps each tile's vertex ranges in the terrain's two line batches. During a preview the
   viewport skips the replaced tiles' ranges and draws the candidate tiles' segments instead.
   `terrainTileContours` generates those segments with the cache's own per-tile generator, run on
   tile contents.
4. **Cancel.** Escape, a tool change and a document change each drop the candidate. The next frame
   draws the original buffers, which were never touched. A preview never writes the document.
5. **Commit.** Commit runs `terrain.sculpt` through the batch API (`ToolSession`) as one "Sculpt
   terrain" or "Smooth terrain" edit. Exports (raster, measured views, sheet views) never draw a
   candidate.

**Overlay.** The overlay (the changed region's hatched black and white band, a region's falloff
band, and the rings) is built when the brush changes, draped with batched elevation queries. A
frame then only projects it.

**Tested bit for bit** (`terrain_sculpt_tests` "desktop preview"):
- the committed tiles equal the candidate;
- every drawn tile's normals equal the committed per-tile surface's, and its outer edge count is
  the same;
- every tile the preview does not draw keeps its surface object;
- each candidate tile's contour segments equal the committed tile's in the assembled contours.

The desktop suite checks the same equality from real input. It also checks that Escape leaves
revision, history, dirty state, encoding and the rendered frame (pixel-equal) unchanged.

**Avoid planning twice.** `terrain.sculpt` used to plan in its dry run and again in
`applyTerrainEdit` (R096.b notes). It now passes its dry run to `applyTerrainEdit` as the planned
outcome. The recorded pieces change source ids only, not the mesh change.

From preview to commit, `previewTerrainSculpt` (when it builds a candidate) remembers its plan in
one thread-safe slot. The slot is keyed by:
- the terrain's record (`BodyPtr`);
- the cached edit mesh object (the same tile records);
- the kind;
- the entry data.

The command takes the slot and uses the plan when every key matches, so the committed tiles are
the previewed ones by construction. The keys match on every commit from the tool:
- the desktop builds the entry data from the command it will commit, with the command's own
  conversion (`sculptData`);
- the release adds no stroke sample.

A miss (any other caller, or different data) plans as before. The test also commits the same change
on a second document with the slot cleared and gets the same tiles. The slot is cleared when it is
taken, on cancel, and when the viewport's document is replaced, so it never keeps a closed
document's mesh.

**Amend (UX §4.3).** `Document::amendLast` refuses a replacement that touches other bodies than
the original edit. A different typed amount, or a wider brush, touches other tiles.
- `executeAmend` chooses the new `AmendPolicy::TerrainTiles` for a single `terrain.sculpt` (and
  for `terrain.stamp_pad`, in f2).
- The policy applies only when the original edit extended one terrain's edit list by exactly one
  entry. A display, settings or replay edit is not revised this way.
- The replacement may replace or create any tiles of that terrain. It must extend the same list by
  one entry of the same kind.
- Everything else is held as under the generic policy: other bodies, other terrains, non-tile
  bodies and every other record kind.
- One History entry remains, keeping its label, and the revision increments.

The tests cover the refusals: a change of kind, a display edit, and the generic policy refusing
other tiles.

**The tools (UX §15.1, §15.2).**
- *Menu, rail, context menu.*
  - The Terrain menu has headings as labels (Tools, and the selected terrain's name), because the
    style draws a QMenu section as a bare separator.
  - Sculpt (`tool.29`, Shift+S) and Smooth (`tool.30`, no default) are model-scope tool actions,
    in command search and in the shortcut editor. The suite checks that Shift+S is Sculpt's only
    binding wherever it can be live, and that no command binds a plain `[` or `]`.
  - The rail's Terrain slot follows a separator after New face. It is in a sixth rail group
    (Terrain) for the narrow rail. While a terrain tool is active it is checked and is the rail's
    Tab stop.
  - Reset edits… (`terrain.reset_edits`) asks first, with Cancel the default. Its replay progress
    chip comes with the Edits list in f2.
- *Strip.*
  - The strip is a frame on the viewport beside the breadcrumb. Where it does not fit there, it
    sits under the breadcrumb, or under the component banner.
  - It is laid out by the shared `FlowLayout` (which now honours its margins), so it wraps.
  - Length fields parse with `parseLength` in document units.
  - Enter applies and keeps focus. Escape restores the field and returns focus to the viewport.
    Keys a control leaves unhandled stop at the strip.
  - F6 goes from the viewport to the strip and from the strip to the Measurements box: while the
    strip shows, both move after the viewport in the region order.
- *Brush.*
  - The ring, the half-strength ring and the label lie on the surface. The half-strength ring is at
    half the radius, where the weight is ½ for smooth and linear falloff; constant falloff has
    none.
  - Dots mark the plan's moving vertices, sized by their weights
    (`TerrainSculptInfluence::weight`), at their candidate height while a candidate shows. Pad
    vertices weigh zero, so they get no dots.
  - A band outlines the changed region: the union of the brush discs (`QPainterPathStroker`), or the
    region and its falloff band.
- *Gestures.*
  - Press and drag sweeps a stroke: a sample every quarter radius, at most 256, thinned when full.
    Release commits the last previewed stroke.
  - A click anchors the one-spot brush. Its amount is the pointer's height above the anchor, in
    metres at the anchor's depth. A click or a typed value commits it.
  - Ctrl lowers and Shift smooths (in Sculpt) while held. `[` and `]` scale the radius by 1.1.
  - Region scope takes a selected face or closed edge loop, or points clicked or typed and closed
    on the first one. A click on the terrain, Enter or a typed value applies it.
  - Choosing the active terrain tool again keeps an anchored spot and the amend.
  - A commit runs exactly the command the shown candidate was built from; a modifier pressed or
    a release sample added since never changes it. A one-spot brush keeps the mode it was
    anchored with (Shift while anchored changes nothing): its amount is set by moving up or down,
    which only a raise has.
  - Typing during a stroke moves focus to the Measurements box, which withdraws the stroke; the
    status bar says "Stroke cancelled · typing sets the Radius; release first to type an amount",
    and the typed text is a radius.
  - During a preview, the hidden tiles also leave the translucent pass and the sun shadow map, and
    the candidate casts the shadows in their place. Exports draw the model as it is.
- *Measurements box.* The step is:
  - before a stroke: Radius (Falloff width in region scope, Point while a region is being drawn);
  - during a stroke, while a spot or region waits for its value, and after a commit (when a typed
    value amends): Amount or Strength, by mode.

  During a stroke the box shows the live value. `[x, y]` (model metres) and `@E, N` (site
  coordinates) place the brush, and the z of a typed position comes from the surface.
- *Refusals.*
  - Over anything but a terrain, or over a terrain outside the open editing context, the cursor is
    not-allowed and the status bar says to point at a terrain.
  - With no terrain in the model, it says "No terrain in this model. Import one with File → Import
    terrain…".
  - A locked, tilted or scaled terrain refuses in the preview with the command's reason, shown in
    the status bar with the not-allowed cursor. The scale check is now shared
    (`requireUnscaledTerrain`).
  - A refusal at the brush is marked with ×.
  - R095.e1's refusal of the modelling tools is unchanged.

**Choices where the design is open, and deviations.**
- **Rail slot label.** The slot reads "Terrain ▸" until a terrain tool has been used, then the last
  one used ("Sculpt ▸"). It starts Sculpt at first. §15.1's text says "Sculpt ▸" at first, but the
  approved mockup's menu screen draws "Terrain ▸" before any tool is used, and the brief asks for
  the option closest to the mockups.
- **`[` and `]`.** In brush scope they change the radius, so a coordinate that starts with `[` is
  typed in the Measurements box. Reach the box with F6 or Tab, or type `@` for site coordinates,
  which opens it. Region scope has no radius, so there `[` opens the box with the bracket, as in
  the drawing tools.
- **Strip hint.** "Ctrl lowers · Shift smooths" wraps left-aligned, not right-aligned as drawn.
- **Menu item.** The Terrain menu shows "Regenerate terrain" (its command search name) where the
  mockup has "Regenerate"; the selected terrain's name heads the group.
- **Preview surfaces** draw the tile's material colour without its texture or translucency.
- **No worker yet.** f1 builds no latest-wins worker: sculpt plans on the UI thread (§5), and f2
  adds the worker for contour edits, stamps and drapes.
- **Rail height.** The rail already overflowed at 860 px window height (New face and the camera
  tools went behind Qt's overflow menu). The Terrain slot has no separator on the wide rail, and
  rail buttons are 1 px shorter (`#toolRail QToolButton`, 10 px top padding instead of 11), so the
  full 19-tool rail still fits a 1000 px window (a maximised window on a 1080p screen): it needs
  909 px with the old padding against 896 px available. Buttons stay above §7's 28 px controls.
  `narrow_layout_input_tests` checks the full rail at 1000 px and records that 15 of the 19 tools
  show at 860 px. The screenshots are taken at 1280 × 1080.
- **Disabled menu items** are now drawn muted (`QMenu::item:disabled`), as the mockup draws them;
  before this, every menu drew them in ink.

**Measured** (from the input event to the presented frame):

`terrain_budget_benchmark --sculpt-frames 64` is new. Conditions:
- **Build and drawing:** Release, GCC 16.2.1, Qt 6.12.0; host Mesa llvmpipe (LLVM 23.1.3, 256 bits)
  drawing to the pinned image's Xvfb at 1920 × 1080.
- **Host:** `systemd-run` with 4 CPUs, 4 GiB and no swap on the development host; load average
  about 3.
- **Harness:** R095.f's. `TimedViewport` times `Viewport::paintGL` plus `glFinish`, and
  `grabFramebuffer` forces every frame, so a "frame" here includes reading it back.
- **Scene:** the two 250k terrains of `terrain_budget_benchmark` in its 20-box model, with 1 m
  contour lines, in top view framing each stroke's neighbourhood, with the Sculpt tool.

Samples:
- **One-tile:** a drag of three samples through the middle of one of the 64 largest tiles, with a
  radius of a quarter of the tile's smaller side and an amount of 0.5 m. Samples whose touched tiles
  hold more than 2,048 triangles are left out (3 of 64 on the points terrain).
- **Preview:** each pointer move that adds a stroke sample, from the event sent to the viewport to
  the completed frame showing the candidate.
- **Commit:** the release, from the event to the completed frame showing the committed tiles.
- **Nine-tile brush:** the same drag with a radius of 1.1 cells, at 16 tiles.
- Each stroke is undone outside the timing.
- Best of three runs (R095.f's method); the three runs' one-tile preview p95 were 112–118 ms and 67–83 ms.

| Terrain | One-tile preview p50 / p95 (max) | of which: event, display build, frame (p95) | Frame without a candidate (p95) | One-tile commit p50 / p95 (max) | of which: event (command and edit), cache rebuild, frame (p95) | Nine tiles: preview / commit p95 |
| --- | ---: | --- | ---: | ---: | --- | ---: |
| 125,000 points (249,970 triangles, 256 tiles; 214 vertices, 1,123 triangles) | 91.2 / 112.3 ms (145.8) | 22.0, 21.3, 101.9 ms | 82.4 ms | 187.8 / 216.4 ms (223.1) | 72.1, 74.9, 148.3 ms | 174.1 / 565.2 ms (13,527 triangles in up to 13 tiles) |
| 416 contour rings (249,298 triangles, 3,169 tiles; 155 vertices, 920 triangles) | 60.4 / 66.9 ms (85.5) | 9.9, 11.2, 58.2 ms | 48.9 ms | 165.2 / 186.6 ms (206.8) | 83.4, 55.8, 100.2 ms | 189.0 / 523.5 ms (8,797 triangles in up to 13 tiles) |

- **Preview, one tile (p95 ≤ 100 ms).**
  - It passes on the contours terrain (66.9 ms) and misses on the points terrain (112.3 ms).
  - The work this slice adds is small: the event (the pick, the plan on the edit mesh and the
    overlay) costs 10–22 ms, and building and uploading the candidate's display costs 11–21 ms.
  - The rest is the frame. llvmpipe alone takes 49–82 ms p95 for the same view without a candidate.
  - R095.f judged frames on the AMD GPU, where a complete 250k frame is p95 0.8–0.9 ms, so the
    preview should be about 30–40 ms there. This has not been measured: the AMD session is a
    hardware session for the owner (R096.g).
- **Commit, one tile (p95 ≤ 100 ms, decision 10).** **Not met** under these conditions: p95 is
  187–216 ms. Leaving the frame aside, the CPU part alone is 130–150 ms:
  - The release event takes 72–83 ms: `terrain.sculpt` through the batch API plus the viewport's
    refresh. R096.b measured 40–65 ms for the command, mostly the whole-terrain SHA-256.
  - The commit frame's cache rebuild takes 56–75 ms: per-tile surfaces and contours,
    re-tessellation of the rebuilt tiles and their seam ring, and re-upload of the terrain's whole
    contour batch.

  Until this slice, the desktop commit also described the whole document in the batch result,
  about 480 ms at 250k. The terrain tools now ask for the changes only (`BatchResponse::Changes`,
  also through `executeAmend`).

  The remaining costs have known fixes, all outside f1's scope:
  - a per-tile digest tree, which R096.a deferred until the frame budget needed the time; it
    changes the digest's definition;
  - per-tile contour GPU batches instead of one batch per terrain;
  - keeping the seam-ring tiles' tessellations when only their normals change.

  These are recorded for R096.g.
- **Nine tiles (reported).** Preview p95 is 174–189 ms and commit p95 is 524–565 ms.
  - The ADR's estimate of about 130 ms for the nine-tile commit and display covered only the core
    commit (101–103 ms in R096.b) and the cache update without the viewport.
  - Here the release also adds the batch and the refresh.
  - The frame re-tessellates up to 13 tiles and their seam ring, re-uploads the contour batch and
    draws at 1920 × 1080 under llvmpipe.

**Verification.** `terrain_edit_input_tests` (ctest `terrain_edit_input`) runs in the native matrix
on X11 and Wayland at 1× and 2×, at 1.5 on both, and at 200 % text. It covers:
- the menu, the rail slot, the context menu, the shortcuts and the conflict check;
- the strip: its syntax, refusals, Escape, F6, wrapping, accessible names, and keeping its keys;
- the stroke preview: the hidden tiles, the overlay draped on the surface, a commit equal to the
  candidate, and Escape leaving revision, history, dirty state, encoding and the frame unchanged;
- the one-spot brush, and amend of amount and strength;
- Ctrl and Shift, and `[` and `]`;
- region scope, from a selected face and from a clicked polygon;
- the keyboard-only path: `[x, y]`, `@E, N`, Smooth, and typed region points;
- an empty brush, pads without moving vertices, and the hover refusals.

`terrain_sculpt_tests` adds "desktop preview" (described above, with the amend policy's refusals).
`narrow_layout_input_tests` lists the Terrain group.

## Implementation notes (R096.f1b)

R096.f1b works on the one-tile sculpt commit budget that R096.f1 measured and did not meet (p95
187–216 ms). Its fixes were recorded for R096.g. The digest's definition stays as it is: the
`terrain-v1` `meshDigest` is still the key-order SHA-256, every pinned digest is unchanged, and C7
holds. Only the time the digest is computed changes.

**The deferred digest** (`TerrainDigest` in `src/core/terrain_records.hpp`).
- `TerrainRecord::meshDigest` is now a `TerrainDigest`, which is either known (text) or deferred.
- `applyTerrainTiles` (the one commit path of every terrain edit) no longer hashes the whole
  terrain. It records a deferred digest instead. The deferred digest holds the new tiles' immutable
  records in key order and computes `terrainMeshDigest` of them once.
- **Readers block or compute on demand.** `value()` and every conversion to text (encoding,
  `terrain.describe`, comparisons, messages) first read an atomic "done" flag (acquire). Once the
  value is computed, they read the immutable text without locking. Before that, the first reader
  computes the value under the digest's mutex; any other reader waits for it, and the result is
  then published with a release store. A reader never sees a missing or partial value. If the
  hash throws, the digest stays pending and the next reader computes it.
- **Saves never write a stale digest.** The encoder reads `value()`. The hashed tiles are the
  tiles the commit stored (or, before the commit stores them, their originals, which have the same
  geometry). Both are immutable, so the value is the digest of the record's own tiles whenever it
  is computed.
- **Undo and Redo** restore the record object of each history step. Copies of a record share one
  deferred state, so they restore the same value, computed at most once.
- **The definition is unchanged:** the same function on the same tiles in the same order.
  `validateTerrainRecord` skips the SHA-256 text check for a pending digest, because the text is
  well formed by construction and checking it would compute it. `TerrainRecord::operator==`
  compares the digest last, so that the cheap fields decide first.
- **Identity.** A deferred digest knows the tile records it describes (`describes`):
  - **Weak.** It holds each tile's address and a weak reference. An address counts only while its
    reference is live, so a reused address can never match. The tiles to hash are held strongly
    only until the digest is computed.
  - **Follows the commit.** `Document::applyEdit` stores a copy of every record it commits: the
    batch's staged document, then the document itself. At each copy the digest follows its tiles
    to the stored copies (`rebind`, while the originals are alive), so it keeps describing the
    document's own tiles. The validation oracle's private reference run does not rebind.
  - **Never computes.** `describes` locks only the small identity table, never the hash.
  - `terrainMeshDigest(doc, terrain)` returns the record's digest without hashing a second time
    when the digest describes the current tiles.
  - `terrainChangedSinceGenerated` (the Terrain section's Changed chip, read after every commit)
    answers "unchanged" from the identity without computing at all.
- **History charge.** The identity table is charged to terrain history: whole in
  `terrainRecordBytes`, and in `terrainRecordChangeBytes` only when the two records do not share
  the state. That is 24 bytes a tile, about 76 KB for the 3,169-tile contours terrain.
- **Prefetch.** After a commit or an amend, the viewport queues a task on the global
  `QThreadPool` (`TerrainDigest::prefetchTask`) that computes the digest off the UI thread.
  - The task holds the digest weakly. If no record holds the digest any more (an amend replaced
    it, or the document closed), the task does nothing, and it keeps no tiles alive.
  - It catches every exception.
  - A later save usually finds the value ready. If a reader comes first, that reader computes the
    value, and the task then finds it done.
- `TerrainDigest::computations()` counts computed digests for tests and the benchmark. No commit
  event computes one on the UI thread. With the Terrain section shown, the worker's hash can
  finish inside the longer event and is counted there; with the prefetch disabled, the count is
  0.

**The display path after a commit.**
1. **Seam-ring normals are patched, not re-tessellated.** A kept tile in the seam ring changes only
   its normals. Its tessellation, colours, edges and texture coordinates stay as they are.
   - A full build now records, for each opaque vertex of a terrain tile's own mesh, the local
     triangle and corner it came from (`BodyMesh::terrainCorners`). It also records whether the
     corner's normal fell back to the tile's own shading normal, which happens when its position
     is not in the surface.
   - When every other input is the same, the rebuild recomputes each vertex's light from the new
     surface normal (with the same arithmetic as a full build, with and without the sun) and
     re-uploads only the opaque buffer. The other inputs are the record, world transform, bands
     and range, alpha, presentation revision, outer edges and render origin; the tile also needs
     no section cut, no translucent pass and an unshared mesh.
   - A tile is built in full if a corner changes between a surface normal and the fallback.
2. **Contours are uploaded per tile row.** A terrain's contour lines are one pair of GPU batches
   (ordinary and index lines) per row of tile keys. These replace the two batches per terrain.
   - A row whose tile records are all the same as before keeps its buffers. Interval, offset,
     world, theme and origin must also be unchanged. A tile's segments depend only on its record
     and those values.
   - The preview's per-tile skip ranges are kept per row.
   - A one-tile commit re-uploads one or two rows instead of the terrain's whole batch.
3. **The surface cache finds the seam ring through incidence.** `TerrainSurfaceCache` used to test
   every vertex of every kept tile against the changed positions. It now looks up the changed
   positions in the edit mesh's vertex incidence (`trianglesAt`), which gives the tiles that hold
   them.
4. **The terrain record changes alone.** A commit changes the terrain container's record, so every
   tile's ancestor chain counted as changed and every tile was re-examined. When the container
   differs only in its `terrain` record (its transform, visibility, tag, lock and the rest are
   equal), the kept tiles keep the unchanged-chain fast path.
5. **Per-body work is hoisted in `buildMesh`.** It covers active and dim state, reflection and the
   light direction. Hidden state is now computed per triangle and per edge rather than per vertex.
   A terrain tile builds its `ShadingNormals` only if a position is missing from the surface.

**Tests.**
- `terrain_sculpt_tests` adds "deferred digest". It checks:
  - a commit leaves the digest pending, with no digest computed;
  - "changed since generated" during pending answers without computing, with and without the
    validation oracle;
  - the history charge includes the identity table;
  - `terrain.describe` during pending reports the eager digest;
  - a save (`encodeDocument`) during pending writes the final digest, and reopening gives an equal
    document;
  - Undo and Redo during pending restore the right digests;
  - a worker computing the value while readers on other threads read it, with every reader seeing
    the eager value;
  - C7 after the edits.
- `terrain_sculpt_tests` also adds "deferred digest state", on a synthetic hash:
  - `pending` and `describes` answer while a hash is in flight without waiting for it;
  - the hashed tiles are released once computed, and the identity then holds them weakly, so an
    expired tile no longer matches its address;
  - a prefetch of a digest no record holds computes nothing;
  - a throwing hash leaves the digest pending, and its prefetch task does not throw.
- `terrain_edit_input_tests` adds "incremental display". For material and elevation-band shading
  with contours, a stroke from real input must patch seam-ring tiles (`terrainNormalPatches`). The
  committed frame must then equal, pixel for pixel, the frame after a forced full rebuild, and the
  deferred digest must equal the eager one.
- Bit equality from preview to commit and every R096 consistency check pass unchanged: the full
  ctest (228 tests), the terrain suites, and the three terrain desktop suites on X11 at 1× and 2×
  and Wayland at 2×.

**Measured.** The conditions are R096.f1's: `terrain_budget_benchmark --sculpt-frames 64`, Release,
host llvmpipe on the pinned image's Xvfb at 1920 × 1080, `systemd-run` with 4 CPUs, 4 GiB and no
swap, and the same strokes. The figures are the best of three runs, with the ranges of all runs.
The benchmark now splits the release event into the command (`terrain.sculpt` through
`ToolSession`) and the viewport's refresh, and the frame's cache rebuild into its terrain views
(surfaces and contours) and the rest.

The **CPU part** is the release event plus the commit frame's cache rebuild, summed per stroke.
The **llvmpipe frame** is the complete frame (`paintGL` plus `glFinish`) minus that rebuild. On
the AMD GPU a complete 250k frame is p95 0.8–0.9 ms (R095.f).

| One-tile commit, p95 | Points (249,970 triangles, 256 tiles) before | after | Contours (249,298 triangles, 3,169 tiles) before | after |
| --- | ---: | ---: | ---: | ---: |
| Input event to presented frame, p50 / p95 (max) | 176.9 / 192.3 (204.5) | 95.2 / 115.6 (120.3) | 160.3 / 179.1 (182.1) | 94.0 / 106.1 (110.3) |
| All runs, p95 | 192.3–198.6 | 114.5–116.6 | 179.1–238.3 | 106.1–108.1 |
| Release event | 64.1 | 38.0 | 80.5 | 44.1 |
| of which: command / refresh | — | 30.9 / 11.1 | — | 39.1 / 4.4 |
| Commit frame's cache rebuild | 64.9 | 16.4 | 54.2 | 18.9 |
| of which: terrain views | — | 6.6 | — | 9.8 |
| **CPU part** (event + rebuild) | about 129 | **53.0** (51.3–53.2) | about 135 | **63.7** (62.6–63.7) |
| Complete frame (rebuild and llvmpipe drawing) | 131.9 | 81.5 | 98.1 | 61.0 |
| Frame without a candidate (llvmpipe alone) | 76.4 | 73.9 | 48.1 | 44.6 |

The "before" CPU part sums the two p95 values, because the earlier harness did not record the
split.

| Nine tiles (13 touched), p95 | Points before | after | Contours before | after |
| --- | ---: | ---: | ---: | ---: |
| Commit, all runs | 537.8–544.9 | 407.2–417.0 | 453.9–458.2 (652.4 in one loaded run) | 341.1–363.0 |
| CPU part | — | 354.8–365.3 | — | 262.7–279.6 |
| of which: command | — | 219.8–228.0 | — | 163.2–169.4 |
| Preview | 162.4–164.6 | 163.9–168.7 | 154.0–245.7 | 138.1–143.3 |

- **One tile.** The CPU part is now p95 51–64 ms, less than half of what it was. That leaves 36–49
  ms of the 100 ms budget for the frame, against about 1 ms for a hardware frame. Under llvmpipe
  the frame still takes 42–65 ms of drawing, so the end-to-end p95 is 106–117 ms. It is not met
  under software rendering, but the software frame alone exceeds what is left. The AMD session
  (R096.g) is still to measure it on hardware.
- **Where the CPU time goes now.** The command (29–39 ms) is the largest part. It plans nothing
  (the preview's plan is reused) and hashes nothing (the digest is deferred). What remains is
  building and validating the edit: `Document::applyEdit` runs once on the batch's staged
  document and once on the real one, with a topology comparison of every replaced tile each
  time. The refresh (4–11 ms) is mostly the first use of the edit mesh cache after the commit,
  for the overlay.
- **Preview** is unchanged in method. Its p95 is 93–98 ms on points and 66–67 ms on contours. The
  hoists in `buildMesh` shave a few milliseconds from building the candidate.
- **Nine tiles** commit 25–30 % faster. The command (163–228 ms) now dominates. It scales with the
  replaced tiles' validation and topology comparison twice per batch, and that cost is not
  terrain-specific. R096.g should look at it if the nine-tile case gets a budget.
- **Not done.** The digest needed neither per-tile hashes nor a Merkle tree. The staged and final
  `applyEdit` passes, and skipping the topology adjacency for a created or erased tile, would
  change the shared edit validation. They are left for R096.g with these numbers.

**Re-measured after the internal review** (the atomic flag, the weak identity that follows the
commit, the weak prefetch). The host was busier this time (load average 3–6 from other sessions),
so the three builds ran interleaved under the same conditions:
- `main` (ca9cca21), three runs;
- the first R096.f1b head (5d9cc86c) and the reviewed code (e1c9e42a), eight runs each, in
  alternating order.

The table gives one-tile commit p95, best run and range.

| Build | Points | Contours |
| --- | ---: | ---: |
| `main` | 232.7 (232.7–244.8) | 199.1 (199.1–213.9) |
| R096.f1b before the review | 118.0 (118.0–149.8) | 112.5 (112.5–167.1) |
| R096.f1b after the review | 123.9 (123.9–219.7) | 103.7 (103.7–142.4) |
| after the review: CPU part / command | 54.6 / 30.6 | 59.4 / 34.9 |
| after the review: nine tiles | 422.9 (422.9–543.4) | 340.5 (340.5–451.7) |

The review's changes cost nothing measurable. With the order reversed, the reviewed build was
ahead on both terrains (123.9–128.7 against 127.4–149.8 ms on points). The differences follow the
software frame, whose baseline moved by up to 40 ms between runs.

**With the Terrain section shown** (`--terrain-section`: the terrain selected and its Entity Info
section refreshed on every change, as the window does), one-tile commit p95 is 161.4–178.7 ms on
points and 268.3–330.1 ms on contours.
- The release event's refresh grows to 53.7–55.0 and 168.6–216.1 ms.
- That time is the section's own refresh. "Changed since generated" no longer computes the digest.
  With the prefetch disabled, no digest is computed during the event, and the refresh costs the
  same.
- The section rescans the whole terrain after every commit: the elevation range over every vertex,
  and `terrainFlatArea`, which looks up every triangle's corners in a set of all contour points.
  The contours terrain has 3,169 tiles and many contour points, hence its larger cost.
- With the section shown, the digest counter shows the worker's hash finishing inside the longer
  event (one per stroke), not a UI-thread computation.
- Caching the section's figures per tile is left as a follow-up, recorded for R096.g.

## Implementation notes (R096.f1c)

R096.f1c removes the two CPU costs that R096.f1b found around a sculpt commit. The first, the
Terrain section's whole-terrain rescan, is removed. The second, the duplicated edit validation of
a batch, is explained below and left in place.

**The Terrain section's figures, per tile** (`TerrainStatisticsCache` in
`src/app/terrain_actions.*`). The section showed figures that it recomputed over the whole terrain
on every refresh: after every commit while the terrain is selected. Those figures are the tile
and triangle counts, the elevation range in world Z over every vertex, and the flat areas.
`terrainFlatArea` looked up each triangle's corners in a set of all contour points.
- **Per tile.** The cache keeps each tile's elevation range and the areas of its flat triangles,
  in face order. A tile's entry is keyed by the tile record: an immutable body, held so that its
  address stays unique.
  - An edit's rebuilt tiles take new IDs, so only they are scanned.
  - The kept tiles are reused, and tiles the terrain no longer has are dropped.
- **Invalidation.** The cache starts over when any input of every tile changes:
  - the document (`Document::identity`);
  - the terrain;
  - its base sources block (the contour points);
  - its world transform, compared bit for bit, which covers a move of the terrain or of a group
    that holds it.
- **The same bits as the full scan.**
  - The cache and `terrainFlatArea` share one per-face test (`flatTriangleArea`).
  - The combination follows the full scan's order: tiles in ID order, then faces. The flat areas
    are added one by one in that order, so the sum is the same floating-point sum.
  - Taking the minimum and maximum of the per-tile minima and maxima keeps the first of equal
    values, as the vertex-by-vertex scan does.
- **Robustness.**
  - A tile is scanned into a separate entry and stored only when complete, so a failure leaves
    no partial entry.
  - The cache drops everything when the section hides, when the terrain is gone, or when another
    document is shown. A hidden section holds no tiles.
- **What is left in the refresh.**
  - Summing the cached flat areas, and the identity check behind "Changed since generated"
    (R096.f1b).
  - The contour-line status, which reads the viewport's contour cache. After a commit, that read
    assembles the new contour set before the frame does. The work is moved, not added: the
    frame's terrain views are cheaper by about the same amount.

**Tests.** `terrain_desktop_tests` "section statistics" uses rings of contours over several tiles
with flat triangles. It compares the cache with the old full scan bit for bit:
- after the import, a raise, a smooth and a lowering;
- after Undo, Redo and two Undos;
- after moving the terrain, grouping it and moving the group;
- across two terrains;
- for a terrain whose import is undone, and for a terrain with every tile erased ("No mesh").

It also checks:
- each stroke rescans exactly its new tiles, and a move rescans every tile;
- the cache holds exactly the terrain's tiles;
- the section's triangle, elevation and flat-area labels equal a new section's (a full scan)
  when shown, after a sculpt and after Undo, and the section rescans only the stroke's tiles;
- a hidden section holds nothing.

**Measured.** The setup is R096.f1b's: `terrain_budget_benchmark --sculpt-frames 64`, with and
without `--terrain-section`, llvmpipe, 4 CPUs. `main` (6b83ffaf) and R096.f1c ran in three
interleaved rounds, alternating order, with load average 3–7 from other sessions. The table gives
p95 in ms, best run (range of the three).

| One-tile commit | Points: `main` | Points: R096.f1c | Contours: `main` | Contours: R096.f1c |
| --- | ---: | ---: | ---: | ---: |
| Without the section | 123.6 (123.6–196.8) | 122.3 (122.3–124.9) | 110.2 (110.2–115.7) | 109.6 (109.6–116.8) |
| **With the section** | 159.0 (159.0–241.4) | **124.3** (124.3–127.6) | 232.1 (232.1–240.1) | **115.7** (115.7–119.1) |
| With the section: refresh | 50.9–74.7 | 13.7–15.9 | 137.3–144.1 | 11.7–13.3 |
| With the section: CPU part | 95.2 | 61.1 | 185.5 | 66.4 |
| Without the section: CPU part | 56.0 | 54.7 | 63.7 | 62.7 |

| Nine tiles, commit | Points: `main` | Points: R096.f1c | Contours: `main` | Contours: R096.f1c |
| --- | ---: | ---: | ---: | ---: |
| Without the section | 420.5 | 420.7 | 359.5 | 351.7 |
| With the section | 456.8 | 433.2 | 465.9 | 363.1 |

- With the section shown, a one-tile commit now costs 2 ms (points) and 6 ms (contours) more at
  p95 than without it, against 35 and 122 ms before.
- Its refresh is 12–16 ms. Part of that is the contour assembly the frame no longer does: the
  CPU part differs by 4–6 ms.
- For nine tiles the difference is 11–13 ms, against 36–106 ms.

**The nine-tile command: the duplicated validation stays.** A desktop commit runs
`Document::applyEdit` twice:
1. **On the batch's staged document** (a snapshot), when `terrain.sculpt` applies its own edit.
   This run keeps the staged document valid for the batch's later commands. It also produces the
   lineage that the batch's composed edit carries.
2. **On the document**, for the batch's composed edit. This is the edit that is published, with
   the batch's label, metadata, lineage and allocator floor. It is the run the validation
   contract, the validation oracle and the fault tests cover.

Measured with temporary probes at nine tiles, each run takes 50–71 ms of the command's 160–256
ms (12–15 ms at one tile). Neither run can be removed safely:
- **Without the second run,** the document would publish an edit that was validated against
  another document object, as another `Edit`. That is only equivalent if `applyEdit` is a pure
  function of the state and the edit, and if the composed edit matches the staged one. Nothing
  enforces either.
  - The two edits already differ in label, metadata, lineage pruning and the allocator floor.
  - A multi-command batch, an amend or a dry run breaks the match.
  - Any later divergence would weaken validation silently.
- **Without the first run,** later commands in a batch would work on an unvalidated staged state.
  The command's refusals would also move to the end of the batch, with other messages.
- **Sharing per-change results between the two runs** needs proof that each check sees the same
  inputs: allocator floors, retired IDs, locks and placements. That holds for a fresh single
  command, but not in general.

The probes also showed where each run's time goes. 23–33 ms (about half) is `compareTopology` on
one-sided changes: the replaced tiles are erased and the rebuilt tiles are created. For those
changes, the adjacency maps it builds cannot report a modified edge, because no edge is in common.
Their implicit topology validation repeats what is already known:
- the freeze validated the new side;
- the old side is a stored body.

Skipping the adjacency for one-sided changes would save about 45–65 ms per nine-tile command and
change no validation outcome. It is a change to the shared report code, though, not to the
duplicate run. It is recorded for R096.g as a separate decision.

## Sources

Retrieved by web search on 2026-10-19.

- SketchUp Help, [Sculpting and fine-tuning terrain](https://help.sketchup.com/en/sketchup/sculpting-and-fine-tuning-terrain) (Smoove, Add Detail, Flip Edge) and [Placing models and objects on your terrain](https://help.sketchup.com/en/sketchup/placing-models-and-objects-your-terrain) (Stamp offset, Drape).
- SketchUp Community, [Sandbox Smoove tool not working properly](https://forums.sketchup.com/t/sandbox-smoove-tool-not-working-properly/21256) (radius in the Measurements box).
- Autodesk Civil 3D Help, [About the surface definition](https://help.autodesk.com/cloudhelp/2015/ENG/Civil3D-UserGuide/files/GUID-EB41E9FE-56DC-4469-8280-2C2C8B7F1B1B.htm), [To swap surface triangle edges](https://help.autodesk.com/cloudhelp/2024/ENG/Civil3D-UserGuide/files/GUID-DB197ACD-0CAE-42BC-97B6-D97603ACAD7D.htm) and the tutorial [Creating a grading](https://help.autodesk.com/cloudhelp/2016/ENU/Civil3D-Tutorials/files/GUID-CC753396-5BFA-4FEE-9122-968B3ED5F9B5.htm).
- Bricsys Help, [TIN definitions for editing](https://help.bricsys.com/en-us/document/bricscad/civil-tools/tin-surfaces/tin-definitions/tin-definitions-for-editing).
- Blender Manual, [Brush falloff](https://docs.blender.org/manual/en/3.3/sculpt_paint/brush/falloff.html).
- QGIS API, [QgsMeshEditForceByPolylines](https://api.qgis.org/api/classQgsMeshEditForceByPolylines.html).
- ArcGIS Pro Help, [Add or remove a breakline from a TIN](https://pro.arcgis.com/en/pro-app/3.4/help/data/tin/add-a-tin-breakline.htm).
