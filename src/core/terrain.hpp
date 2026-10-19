#pragma once
// R095.a terrain core (ADR 0168 §2, §4): a BodyKind::Terrain container whose
// TerrainRecord holds the sources, and whose children are ordinary geometry tiles of at
// most 2,048 triangles; the document site origin and model <-> site conversion.
// Triangulation is not here: callers pass an already-triangulated mesh.
#include "core/model.hpp"
#include "geometry/terrain_progress.hpp"
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
namespace sketchy {
// Local XYZ vertices (terrain frame, metres) plus counter-clockwise triangles.
struct TerrainMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};
// Triangles bucketed by centroid into a square XY grid sized so no tile holds more
// than TerrainLimits::tileTriangles. Each tile lists its triangle indices in input
// order; tiles are row-major (rows by Y, then columns by X), empty cells omitted.
// The tiles partition the triangle set.
struct TerrainTiling {
    double cell{};
    std::vector<std::vector<std::uint32_t>> tiles;
};
// D-044: with `progress`, reports TerrainStage::Tile and may throw TerrainCancelled.
TerrainTiling tileTerrainTriangles(const TerrainMesh &mesh, TerrainProgress *progress = nullptr);
// One tile body. Its vertices take IDs 1..n in the given order and its faces IDs
// n+1..n+m, so the binary mesh stores neither (ADR 0168 §2); edges come from
// Topology::rebuild and every interior edge is soft and smooth.
Body makeTerrainTile(Id id, Id parent, std::string name, const std::vector<Vec3> &vertices,
                     const std::vector<std::array<std::uint32_t, 3>> &triangles);
// The tile's mesh in the same canonical order (vertex index = vertex ID - 1).
TerrainMesh terrainTileMesh(const Body &tile);
// The implied appearance of a tile: interior edges (two faces) soft and smooth.
std::map<Id, EdgeAppearance> terrainTileEdgeAppearances(const Surface &surface,
                                                        const Topology &topology);
// Little-endian tile layout shared by the digest and the terrain-mesh chunk:
// u32 vertex count, u32 triangle count, f64 XYZ per vertex, u32 x3 per triangle.
void appendTerrainTileBytes(std::string &out, const Body &tile);
// Document checks (called by Document; never by tools).
void validateTerrainBody(const Body &body);
void validateTerrainTileAppearance(const Body &tile);
void checkTerrainParent(const Body &body, const Body *parent);
// Terrain containers whose record this edit creates, changes, drops or erases. Only
// terrain operations (build, explode, R096 regeneration) do that.
std::set<Id> terrainRecordChanges(const Edit &edit);
// Refuses a change that alters a tile's mesh, frame or membership unless the edit
// also changes that terrain's record: generic topology tools refuse terrain tiles
// with terrainProtectedMessage (TerrainError, UNSUPPORTED_TARGET).
void checkTerrainChange(const Change &change, const std::set<Id> &recordChanges);
// Throws the same refusal when `body` is a terrain or terrain tile. Tools call it
// where they validate their targets so the refusal precedes any kernel work.
void requireOrdinaryGeometry(const Document &doc, Id body);
// Queries.
bool isTerrain(const Document &doc, Id body);
std::vector<Id> terrainTiles(const Document &doc, Id terrain); // ascending IDs
std::size_t terrainTriangleCount(const Document &doc, Id terrain);
// SHA-256 (lowercase hex) of u32 tile count followed by every tile's bytes in ID order.
std::string terrainMeshDigest(const std::vector<const Body *> &tiles);
std::string terrainMeshDigest(const Document &doc, Id terrain);
// "Changed since generated": the stored digest no longer matches the tiles.
bool terrainChangedSinceGenerated(const Document &doc, Id terrain);
// Operations, each one labelled edit through Document::apply.
struct TerrainCreated {
    Id terrain{};
    std::vector<Id> tiles;
    ChangeReport changes;
};
// Refuses (LIMIT_EXCEEDED) when the document's terrain triangles would exceed
// TerrainLimits::triangles; record.meshDigest is computed here.
TerrainCreated createTerrain(Document &doc, const TerrainMesh &mesh, TerrainRecord record,
                             Id parent = 0, std::string name = "Terrain");
// R095.c: an imported terrain, and the site origin when the document has none yet, as ONE
// labelled edit ("Import terrain"): one history entry, and Undo removes both. `establish`
// must be empty when the document already has a site origin.
TerrainCreated importTerrain(Document &doc, const TerrainMesh &mesh, TerrainRecord record,
                             std::optional<SiteOrigin> establish, std::string name = "Terrain");
// The effect of Explode terrain on the two budgets (R095.e1's confirmation shows it): the
// document's unique modelling faces now and the terrain's triangles that would join them,
// against the 100,000-face modelling limit, and the document's terrain triangles that the
// terrain would free, against TerrainLimits::triangles.
struct TerrainExplodeBudget {
    std::size_t modelFaces{}, terrainFaces{}, faceLimit{};
    std::size_t documentTerrainTriangles{}, terrainLimit{};
    std::size_t facesAfter() const { return modelFaces + terrainFaces; }
    bool fits() const { return facesAfter() <= faceLimit; }
};
// Refuses (NOT_FOUND) a body that is not a terrain.
TerrainExplodeBudget terrainExplodeBudget(const Document &doc, Id terrain);
// The terrain becomes an ordinary group of its tiles and loses its record. Refuses
// (LIMIT_EXCEEDED) when the faces would exceed the modelling limit (terrainExplodeBudget).
ChangeReport explodeTerrain(Document &doc, Id terrain);
// R095.c2 regeneration. Paint that a new mesh cannot keep: tile faces with their own colour,
// material or texture mapping, and tiles with their own material or colour.
struct TerrainPaint {
    std::size_t faces{}, tiles{};
};
TerrainPaint terrainTilePaint(const Document &doc, Id terrain);
// Refuses (INVALID_OPERATION) when the terrain, an ancestor or one of its tiles is locked.
void requireUnlockedTerrain(const Document &doc, Id terrain);
struct TerrainRegenerated {
    bool changed{};
    std::vector<Id> tiles; // the new tiles in row-major order
    ChangeReport changes;
};
// Replaces the terrain's tiles with `mesh` and its record with `record` (meshDigest is
// computed here) as ONE labelled edit. The terrain body keeps its ID, name, parent,
// transform, tag, material, visibility and lock. Tile IDs: the leading new tiles
// (row-major) whose meshes equal the old tiles' at the same position (ascending IDs) keep
// those IDs; from the first difference on, tiles take new IDs from the document allocator
// in row-major order (a tile body's entity IDs never go back, and IDs must ascend in
// row-major order for the digest); old tiles not kept are removed. Tiles are rebuilt
// fresh, so paint on them is dropped (see terrainTilePaint). When nothing would change the
// document is left alone and `changed` is false. Refuses a locked terrain
// (INVALID_OPERATION) and the document terrain limit (LIMIT_EXCEEDED).
TerrainRegenerated regenerateTerrain(Document &doc, Id terrain, const TerrainMesh &mesh,
                                     TerrainRecord record,
                                     std::string label = "Regenerate terrain");
// Establishes the site origin; refuses (INVALID_REQUEST) when one exists.
void setSiteOrigin(Document &doc, SiteOrigin origin);
// Changes the origin and moves by the difference every stored world position: top-level
// bodies (with their guides, reference images and descendants), model-frame section
// planes, saved cameras and clip planes, the model and saved-scene ground heights, and
// the world fallbacks of annotation and paper anchors. Site coordinates are unchanged.
// One edit. Origin-independent: body-local records, annotation text offsets (vectors),
// paper positions, drawing views (they show saved scenes), hosted attachments (host-local)
// and the sun study (latitude, longitude, north angle and time only).
ChangeReport reanchorSite(Document &doc, double easting, double northing, double elevation);
Vec3 modelToSite(const SiteOrigin &origin, Vec3 model);
Vec3 siteToModel(const SiteOrigin &origin, Vec3 site);
inline constexpr std::size_t terrainPointBatch = 200; // Fits the 16 KiB inspection request.
struct TerrainElevation {
    bool found{};
    double elevation{};
    Id tile{}, face{};
};
// Highest terrain surface under each model-space XY point.
std::vector<TerrainElevation> terrainElevationAt(const Document &doc, Id terrain,
                                                 const std::vector<std::array<double, 2>> &points);
} // namespace sketchy
