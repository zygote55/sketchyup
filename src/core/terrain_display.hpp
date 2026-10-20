#pragma once
// R095.e2 terrain display (ADR 0168 "Decisions > Interaction design", Appearance): the display
// settings edit, contour lines derived from the tiles, and the surface data the viewport needs
// to draw a terrain as one smooth surface (normals continuous across tile seams, seams and
// interior edges not drawn). Everything here is derived display: nothing changes the mesh, and
// nothing here is picked or snapped to.
#include "core/terrain.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
namespace sketchy {
// Changes the terrain's display settings as ONE labelled edit ("Terrain display"). Only the
// record's display changes, so the mesh digest and "Changed since generated" do not. Refuses
// NOT_FOUND (not a terrain), INVALID_OPERATION (the terrain or an ancestor is locked) and
// INVALID_REQUEST (out-of-range settings). Equal settings change nothing (an empty report).
ChangeReport setTerrainDisplay(Document &doc, Id terrain, const TerrainDisplay &display);

// ---- Contour lines --------------------------------------------------------------------------
// Bounds: at most this many segments per terrain (about 28 MB of segments at 250,000
// triangles); an interval that would need more draws no contours and says so (`tooDense`).
inline constexpr std::size_t terrainContourSegmentLimit = 500000;
inline constexpr int terrainContourIndexEvery = 5;
struct TerrainContourSegment {
    Vec3 a, b;  // Terrain frame (the tiles' shared frame).
    bool index{}; // Every fifth level (elevation a multiple of 5 x interval) is an index line.
};
struct TerrainContours {
    double interval{}, elevationOffset{};
    std::vector<TerrainContourSegment> segments;
    std::size_t levels{}, indexLevels{}; // Distinct levels with at least one segment.
    std::size_t estimatedSegments{};     // Upper bound counted before generating.
    bool tooDense{};                     // estimatedSegments exceeded the limit: no segments.
};
// Levels are at elevations e = z + elevationOffset that are whole multiples k of `interval`
// (z in the terrain frame; the offset turns it into a site elevation), index when k is a
// multiple of five. A vertex at or above a level counts as above it, so a triangle meets a
// level when min(e) < level <= max(e); each crossed edge is interpolated from its endpoints in
// a canonical order, so the two triangles sharing an edge give bit-identical points and the
// result depends only on the tiles in order (deterministic). O(triangles + segments).
TerrainContours terrainContours(const std::vector<const Body *> &tiles, double interval,
                                double elevationOffset);
// The offset of the terrain frame's Z to site elevation: the frame's world Z translation plus
// the site origin's elevation (0 without a site origin).
double terrainElevationOffset(const Document &doc, Id terrain);

// Cached contours per terrain and, within it, per tile (R096.a; ADR 0169 §5): keyed by the
// interval and the elevation offset, and per tile by its record (identity) or, for a changed
// record with the same mesh (paint), by the mesh. A lookup with the same tile records returns
// the cached contours; after an edit only the rebuilt tiles' segments are generated and the
// terrain's are assembled in tile-key order (for a terrain never edited, ID order: the R095
// result). When every tile is reused in the same order the previous contours are returned. The
// segment bound applies to the sum of the tiles' estimates. Bounded: entries for terrains that
// no longer exist are dropped, and at most `capacity` terrains are kept (least recently used
// first out).
class TerrainContourCache {
  public:
    explicit TerrainContourCache(std::size_t capacity = 8) : capacity_(capacity) {}
    std::shared_ptr<const TerrainContours> get(const Document &doc, Id terrain);
    void prune(const Document &doc);
    std::size_t size() const { return entries_.size(); }
    // Terrain contours assembled, and tile contours generated, since construction.
    std::size_t generations() const { return generations_; }
    std::size_t tileGenerations() const { return tileGenerations_; }

  private:
    struct Tile {
        BodyPtr body;
        std::vector<TerrainContourSegment> segments;
        std::vector<std::int64_t> levels; // ascending
        double estimate{};
    };
    struct Entry {
        std::vector<BodyPtr> tiles; // key order
        double interval{}, offset{};
        std::map<Id, std::shared_ptr<const Tile>> perTile;
        std::shared_ptr<const TerrainContours> contours;
        std::uint64_t used{};
    };
    std::size_t capacity_;
    std::map<Id, Entry> entries_;
    std::uint64_t clock_{};
    std::size_t generations_{}, tileGenerations_{};
};

// ---- Surface ---------------------------------------------------------------------------------
// Per terrain: area-weighted vertex normals by exact terrain-frame position over ALL tiles (a
// border vertex is duplicated in each tile; its copies share one normal, so shading is
// continuous across seams), the edges on the terrain's outer boundary (the only tile edges
// drawn: an edge used by two triangles anywhere in the terrain, inside a tile or across a seam,
// is not), and the terrain-frame elevation range.
struct TerrainSurfaceDisplay {
    struct Key {
        std::uint64_t x{}, y{}, z{};
        bool operator==(const Key &) const = default;
    };
    struct Hash {
        std::size_t operator()(const Key &key) const;
    };
    static Key key(Vec3 point);
    std::unordered_map<Key, Vec3, Hash> normals;
    std::map<Id, std::set<Id>> outerEdges; // tile -> edge IDs on the outer boundary
    double zMin{}, zMax{};
    Vec3 normal(Vec3 point, Vec3 fallback) const;
    bool outerEdge(Id tile, Id edge) const;
};
// The tiles are taken in tile-key order (sorted here), so each position's normal sums its
// triangles in (tile key, face) order, the order the per-tile surfaces below use.
TerrainSurfaceDisplay terrainSurfaceDisplay(const std::vector<const Body *> &tiles);

// R096.a per-tile surface display (ADR 0169 §5): the same normals and outer edges, held per tile
// so an edit recomputes only the tiles it rebuilt and the tiles sharing a position with them
// (the seam ring); every other tile keeps its object (pointer-identical), so the viewport keeps
// its tessellation. Each tile's normals and outer edges are bit-equal to terrainSurfaceDisplay's.
struct TerrainTileSurface {
    std::unordered_map<TerrainSurfaceDisplay::Key, Vec3, TerrainSurfaceDisplay::Hash> normals;
    std::set<Id> outerEdges;
    Vec3 normal(Vec3 point, Vec3 fallback) const;
    bool outerEdge(Id edge) const { return outerEdges.contains(edge); }
};
struct TerrainTileSurfaces {
    std::map<Id, std::shared_ptr<const TerrainTileSurface>> tiles; // by tile body
    double zMin{}, zMax{};
    std::shared_ptr<const TerrainTileSurface> tile(Id id) const {
        const auto found = tiles.find(id);
        return found == tiles.end() ? nullptr : found->second;
    }
};
// Per terrain, keyed by its tile records; a lookup after an edit updates the previous surfaces
// from the tiles the edit replaced. Uses the edit mesh (terrainEditMeshCache) for adjacency.
// Bounded as TerrainContourCache.
class TerrainSurfaceCache {
  public:
    explicit TerrainSurfaceCache(std::size_t capacity = 8) : capacity_(capacity) {}
    std::shared_ptr<const TerrainTileSurfaces> get(const Document &doc, Id terrain);
    void prune(const Document &doc);
    std::size_t size() const { return entries_.size(); }
    std::size_t tileComputations() const { return computations_; }

  private:
    struct Entry {
        std::vector<BodyPtr> tiles; // ascending ID
        std::shared_ptr<const TerrainTileSurfaces> surfaces;
        std::uint64_t used{};
    };
    std::size_t capacity_;
    std::map<Id, Entry> entries_;
    std::uint64_t clock_{};
    std::size_t computations_{};
};

// Elevation-band shading: ten bands over [zMin, zMax] from green through sand to brown (the
// import preview's ramp). `z` and the range are in one frame.
std::array<float, 3> terrainElevationBandColor(double z, double zMin, double zMax);
} // namespace sketchy
