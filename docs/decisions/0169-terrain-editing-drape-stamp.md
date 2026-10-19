# Terrain editing, drape and stamp

Status: **Accepted (2026-10-19)** for the decisions below; the UX design is pending
owner approval. Roadmap [R096](../PR_ROADMAP.md#r096) (M10, scope row
[F03](../SCOPE.md)); builds on [ADR 0168](0168-terrain-import-and-surfaces.md) (R095).
Nothing is implemented. Each section gives the options and trade-offs that were
weighed; the owner chose the recommended option in every case (see "Decisions"). The
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
| C1 | Every **current** source contour and breakline lies on the surface. Each vertex is a mesh vertex with Z exactly its stored value (bit-equal; contours at their one elevation), and each segment is a mesh edge. |
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
| `terrain.drape` | `{terrain, edges?, faces?, mode: imprint\|overlay, paintRegions?, lift?}` | "Drape on terrain" |
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

**Interaction design.** Pending. The owner approves a UX design for §8's list
separately, as for R095; R096.a–e (core and commands) do not depend on it, and R096.f
does.

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

## Sources

Retrieved by web search on 2026-10-19.

- SketchUp Help, [Sculpting and fine-tuning terrain](https://help.sketchup.com/en/sketchup/sculpting-and-fine-tuning-terrain) (Smoove, Add Detail, Flip Edge) and [Placing models and objects on your terrain](https://help.sketchup.com/en/sketchup/placing-models-and-objects-your-terrain) (Stamp offset, Drape).
- SketchUp Community, [Sandbox Smoove tool not working properly](https://forums.sketchup.com/t/sandbox-smoove-tool-not-working-properly/21256) (radius in the Measurements box).
- Autodesk Civil 3D Help, [About the surface definition](https://help.autodesk.com/cloudhelp/2015/ENG/Civil3D-UserGuide/files/GUID-EB41E9FE-56DC-4469-8280-2C2C8B7F1B1B.htm), [To swap surface triangle edges](https://help.autodesk.com/cloudhelp/2024/ENG/Civil3D-UserGuide/files/GUID-DB197ACD-0CAE-42BC-97B6-D97603ACAD7D.htm) and the tutorial [Creating a grading](https://help.autodesk.com/cloudhelp/2016/ENU/Civil3D-Tutorials/files/GUID-CC753396-5BFA-4FEE-9122-968B3ED5F9B5.htm).
- Bricsys Help, [TIN definitions for editing](https://help.bricsys.com/en-us/document/bricscad/civil-tools/tin-surfaces/tin-definitions/tin-definitions-for-editing).
- Blender Manual, [Brush falloff](https://docs.blender.org/manual/en/3.3/sculpt_paint/brush/falloff.html).
- QGIS API, [QgsMeshEditForceByPolylines](https://api.qgis.org/api/classQgsMeshEditForceByPolylines.html).
- ArcGIS Pro Help, [Add or remove a breakline from a TIN](https://pro.arcgis.com/en/pro-app/3.4/help/data/tin/add-a-tin-breakline.htm).
