#pragma once
// R096.a edit core (ADR 0169 §2, §3): stable tiling, local tile replacement, the seam
// invariant, the edit-kind registry and the incremental application of one edit entry.
//
// The pieces, in the order an edit uses them:
// 1. An edit kind computes a TerrainRegionChange against the terrain's TerrainEditMesh: the
//    triangles it removes, the vertices it lifts (Z only) and the triangles it adds.
// 2. replaceTerrainRegion turns the change into new tile contents for the touched leaves of
//    the stored grid only (a pure function of the mesh and the change).
// 3. applyTerrainTiles commits them with the new record as ONE labelled edit: a tile whose key
//    and mesh are unchanged keeps its body and ID; a rebuilt tile takes a new ID; every other
//    tile is untouched (pointer-identical).
// The replay engine (R096.a part 2) runs the same steps on an in-memory mesh.
#include "core/terrain.hpp"
#include "core/terrain_edit_mesh.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace sketchy {
inline constexpr const char *terrainEditGenerator = "cdt-2.0.0/terrain-v1+edit-v1";

// ---- Stable tiling -------------------------------------------------------------------------
// The tiles of a freshly triangulated mesh with their keys (row, column, "" path, part), in
// key order, and the grid (tileTerrainTriangles' cell and the minimum centroid as origin).
struct TerrainTileSet {
    TerrainTileGrid grid;
    std::vector<TerrainTilePtr> tiles;
};
TerrainTileSet terrainTileSet(const TerrainMesh &mesh, TerrainProgress *progress = nullptr);
// The key name of a tile created by an edit (a tile that replaces one with the same key keeps
// that tile's name): "Terrain tile R<row> C<column>", then " Q<path>" and " part <n>" when set.
std::string terrainTileKeyName(const TerrainTileKey &key);
// Row and column of a point's cell in the grid, and its quadrant digit at depth d >= 1.
std::optional<std::array<std::int64_t, 2>> terrainCellOf(const TerrainTileGrid &grid, double x,
                                                         double y);
int terrainQuadrantDigit(const TerrainTileGrid &grid, double x, double y, std::size_t depth);

// ---- Local tile replacement ----------------------------------------------------------------
struct TerrainRegionChange {
    std::vector<TerrainTriangleRef> removed;           // triangles removed
    std::vector<std::pair<Vec3, double>> lifted;        // every copy of a position takes this Z
    std::vector<std::array<Vec3, 3>> added;             // new triangles, counter-clockwise in plan
};
struct TerrainRegionResult {
    std::vector<TerrainTileKey> replaced; // old tiles rebuilt or dropped, key order
    std::vector<TerrainTilePtr> tiles;    // the new tiles of the touched leaves, key order
    std::size_t trianglesBefore{}, trianglesAfter{}; // of the whole terrain
};
// The touched leaves are those of tiles holding a removed triangle or a lifted vertex, and the
// leaves (by centroid, descending the cell's existing quadrant structure) of added triangles.
// Each touched leaf is rebuilt from its old triangles (parts in order, triangles in order) minus
// the removed ones, with lifts applied, plus the added ones landing in it (in order); a leaf over
// 2,048 triangles splits into quadrants, recursively, and at terrainTileMaxDepth into parts in
// triangle order. Refuses (TerrainError INVALID_REQUEST) a missing triangle or lifted position,
// a non-finite or out-of-range point, an added triangle with repeated corners, and a centroid
// outside the representable grid.
TerrainRegionResult replaceTerrainRegion(const TerrainEditMesh &mesh,
                                         const TerrainRegionChange &change);
// The mesh after `result` (a copy is updated; the edit mesh of the document cache is not).
void applyTerrainRegion(TerrainEditMesh &mesh, const TerrainRegionResult &result);

struct TerrainTilesApplied {
    std::vector<Id> removed, added; // tile bodies
    TerrainPaint paintDropped;     // per-face paint on replaced tiles; tiles' own paint is kept
    ChangeReport changes;
};
// Commits `result` and `record` (meshDigest is computed here, over the tiles in key order) as
// ONE labelled edit. A new tile with the key and mesh of a replaced tile keeps that body (and
// its ID); otherwise it takes a new ID from the document allocator in key order, the material
// sides and colour of the replaced tile with its key (or of the first replaced tile of its
// cell, else the terrain's shared tile material) and per-face paint is dropped (counted).
// Refuses NOT_FOUND, a locked terrain (INVALID_OPERATION), the document terrain limit
// (LIMIT_EXCEEDED) and the history budget.
TerrainTilesApplied applyTerrainTiles(Document &doc, Id terrain, TerrainRecord record,
                                      const TerrainRegionResult &result, std::string label);

// ---- Seams (ADR 0169 §3, check C6) ---------------------------------------------------------
// Merged by exact plan position: every position has one Z in all its copies; every directed
// edge is used at most once (consistent orientation, edge-manifold); and no edge used by one
// triangle (outer or hole boundary) has another vertex of the terrain strictly inside it in
// plan (a crack or T-junction between tiles). `problems` names at most 32 occurrences.
struct TerrainSeamReport {
    std::size_t positions{}, edges{}, boundaryEdges{};
    std::size_t zMismatches{}, nonManifoldEdges{}, cracks{};
    std::vector<std::string> problems;
    bool ok() const { return !zMismatches && !nonManifoldEdges && !cracks; }
};
TerrainSeamReport checkTerrainSeams(const std::vector<TerrainTilePtr> &tiles);
TerrainSeamReport checkTerrainSeams(const Document &doc, Id terrain);

// ---- Edit kinds (ADR 0169 §2) --------------------------------------------------------------
// An entry replays against the mesh state before it. `apply` returns the change, or nullopt when
// the entry can no longer apply (it is then skipped with `notice` and kept in the list). It must
// be a pure function of the mesh's geometry and the entry: it may not depend on slot numbers,
// hash-map iteration order or threads (ADR 0169 §11).
struct TerrainEditOutcome {
    std::optional<TerrainRegionChange> change;
    std::string notice; // why it was skipped (located), when change is empty
};
// R096.c: what a kind may read besides the mesh: the current sources and protected regions
// before the entry (core/terrain_sources.hpp, derived from the base sources and the entries
// before it), and the terrain's parameters (the flat-triangle rule).
class TerrainSourceState;
struct TerrainEditContext {
    const TerrainEditMesh &mesh;
    const TerrainSourceState &sources;
    const TerrainParameters &parameters;
};
// A plan polygon (terrain frame, counter-clockwise or not, closing edge implied).
using TerrainPlanPolygon = std::vector<std::array<double, 2>>;
struct TerrainEditKind {
    std::string name;
    std::function<TerrainEditOutcome(const TerrainEditMesh &, const TerrainEdit &)> apply;
    // Plan bounding box of the entry's influence (terrain frame): minX, minY, maxX, maxY.
    std::function<std::array<double, 4>(const TerrainEdit &)> bounds;
    // R096.c: a kind that reads the current sources applies through this instead of `apply`.
    std::function<TerrainEditOutcome(const TerrainEditContext &, const TerrainEdit &)>
        applyInContext;
    // R096.c: the entry's effect on the current sources (source edits). It reads no mesh, so
    // the current sources of a record are derived without triangulating (ADR 0169 §2); it runs
    // for every entry in order, applied or skipped, in replay and in the derivation alike.
    std::function<void(TerrainSourceState &, const TerrainEdit &)> sources;
    // R096.c, for R096.d: the plan polygons the entry protects (pads, ADR 0169 decision 6).
    // Later source edits whose geometry enters one are refused ("inside-pad").
    std::function<std::vector<TerrainPlanPolygon>(const TerrainEdit &)> protects;
};
// The kinds R096 defines (R096.a: none; R096.c: editSource, addSource and removeSource; R096.b,
// d and e add sculpt, smooth, stamp and drape) plus any registered by tests. Registration is for
// tests and the benchmark (terrain_edit_benchmark's test-only operation); it is not thread-safe
// and must happen before any terrain work.
void registerTerrainEditKind(TerrainEditKind kind);
const TerrainEditKind *findTerrainEditKind(const std::string &name);

struct TerrainEditApplied {
    std::uint32_t edit{};          // the new entry's ID
    TerrainTilesApplied tiles;
};
// Appends `data` as a new entry of `kind` and applies it to the terrain incrementally: ONE
// labelled edit of the record (the new list, the generator) and the touched tiles. Refuses
// NOT_FOUND (terrain), INVALID_REQUEST (unknown kind, an entry that does not apply),
// LIMIT_EXCEEDED (4,096 entries or 8 MiB of edit data, naming Reset edits and Explode terrain;
// the terrain limit), INVALID_OPERATION (locked) and a frame that is not level.
TerrainEditApplied applyTerrainEdit(Document &doc, Id terrain, const std::string &kind,
                                    std::shared_ptr<const TerrainEditData> data,
                                    std::string label);
// R096.c (ADR 0169 §5, the terrain preview path): the candidate of the same edit, computed on
// the cached edit mesh exactly as applyTerrainEdit computes it, with nothing validated or
// committed. Committing the same data gives these tiles bit for bit (tested). Refuses as
// applyTerrainEdit does before its commit.
struct TerrainEditPreview {
    std::uint32_t edit{};           // the ID the entry would take
    TerrainRegionResult region;     // the touched tiles' candidates, key order
    std::size_t removedTriangles{}, addedTriangles{}, liftedVertices{};
};
TerrainEditPreview previewTerrainEdit(const Document &doc, Id terrain, const std::string &kind,
                                      std::shared_ptr<const TerrainEditData> data);
// ---- Replay (ADR 0169 §2) -------------------------------------------------------------------
// Regenerate, Terrain settings, Reset edits and Remove change rebuild the terrain as the CDT of
// its base sources followed by a replay of its edit list. Each entry replays against the mesh
// state before it, through the same kind, replaceTerrainRegion and leaf rules as the
// incremental edit, so a replay of the list a terrain was edited with gives the same tiles
// (keys, names and meshes) as those edits did ("Regenerate after edits is a no-op", C7). An entry
// that can no longer apply is skipped with its notice and stays in the list. With `progress`,
// the replay reports TerrainStage::Replay per entry and throws TerrainCancelled when cancelled;
// nothing is changed (the replay works on its own mesh).
struct TerrainReplaySkip {
    std::uint32_t edit{};
    std::string kind, notice;
};
struct TerrainReplayResult {
    TerrainTileSet tiles;            // key order
    std::size_t applied{};
    std::vector<TerrainReplaySkip> skipped;
};
// R096.c: kinds that read the current sources see them derived from `sources` (the base
// sources) and the entries before them, with `parameters` (the replay's, which Terrain settings
// may have changed).
TerrainReplayResult replayTerrainEdits(const TerrainTileSet &base,
                                       const std::vector<TerrainEditPtr> &edits,
                                       TerrainProgress *progress = nullptr,
                                       const TerrainSources &sources = {},
                                       const TerrainParameters &parameters = {});

// Refuses (INVALID_OPERATION) a terrain whose frame is not level: edits are vertical in the
// terrain frame, and "terrains are created level" (ADR 0169 §3).
void requireLevelTerrain(const Document &doc, Id terrain);
} // namespace sketchy
