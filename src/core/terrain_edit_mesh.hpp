#pragma once
// R096.a edit mesh (ADR 0169 §3, "Edit mesh"): one terrain's tiles with the cross-tile
// adjacency that edit operations need, and a uniform grid index over the stored tile cells.
//
// - Tiles are kept by key (TerrainTileContent: the key, the name and the canonical mesh of a
//   tile body: vertex index = vertex ID - 1, triangle index = face position).
// - The grid index maps each stored cell to the tiles whose plan bounding box overlaps it;
//   terrainElevationAt and point location use it instead of scanning every tile.
// - The topology (vertices merged by exact terrain-frame position, as TerrainSurfaceDisplay
//   merges them for normals, and each merged vertex's incident triangles) is built on first
//   use and then kept up to date by insertTile and eraseTile.
//
// Determinism: slot numbers and incidence order depend on the order tiles were inserted.
// Nothing observable may depend on them: callers order by tile key and triangle index
// (TerrainTriangleRef ordering) or by position.
#include "core/model.hpp"
#include "core/terrain_records.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace sketchy {
struct TerrainTileContent {
    TerrainTileKey key;
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    bool sameMesh(const TerrainTileContent &other) const {
        return vertices == other.vertices && triangles == other.triangles;
    }
};
using TerrainTilePtr = std::shared_ptr<const TerrainTileContent>;
// The content of a tile body (its key, name and canonical mesh).
TerrainTilePtr terrainTileContent(const Body &tile);
// A triangle of the terrain: its tile and its index in the tile.
struct TerrainTriangleRef {
    TerrainTileKey tile;
    std::uint32_t triangle{};
    auto operator<=>(const TerrainTriangleRef &) const = default;
    bool operator==(const TerrainTriangleRef &) const = default;
};
// Exact position key (+0.0 and -0.0 are one position), as TerrainSurfaceDisplay::key.
struct TerrainPositionKey {
    std::uint64_t x{}, y{}, z{};
    bool operator==(const TerrainPositionKey &) const = default;
    static TerrainPositionKey of(Vec3 point);
};
struct TerrainPositionHash {
    std::size_t operator()(const TerrainPositionKey &key) const;
};

class TerrainEditMesh {
  public:
    struct Tile {
        TerrainTilePtr content;
        Id body{}; // The tile body, when built from a document (0 otherwise).
        double minX{}, minY{}, maxX{}, maxY{}, minZ{}, maxZ{};
    };
    explicit TerrainEditMesh(TerrainTileGrid grid = {}) : grid_(grid) {}
    const TerrainTileGrid &grid() const { return grid_; }
    // Inserts a tile (replacing one with the same key) or erases one by key.
    void insertTile(TerrainTilePtr tile, Id body = 0);
    void eraseTile(const TerrainTileKey &key);
    const Tile *tile(const TerrainTileKey &key) const;
    // Every tile, in key order.
    std::vector<const Tile *> tiles() const;
    std::size_t tileCount() const { return keys_.size(); }
    std::size_t triangleCount() const { return triangles_; }
    // Tiles of one stored cell (row, column), in key order.
    std::vector<const Tile *> cellTiles(std::int64_t row, std::int64_t column) const;
    // Tiles whose plan bounding box, widened by `slack`, contains (x, y); in key order.
    std::vector<const Tile *> tilesAt(double x, double y, double slack) const;
    // R096.c: tiles whose plan bounding box, widened by `slack`, overlaps the box; in key order.
    std::vector<const Tile *> tilesIn(double minX, double minY, double maxX, double maxY,
                                      double slack) const;

    // ---- Topology (built on first use) ----
    // The triangles at an exact position, in TerrainTriangleRef order; empty when no vertex is
    // there.
    std::vector<TerrainTriangleRef> trianglesAt(Vec3 point) const;
    // Whether the topology has been built (it is then maintained incrementally).
    bool topologyBuilt() const { return topologyBuilt_; }
    std::size_t mergedVertexCount() const;
    // The merged vertices' positions (unordered; the caller sorts if it needs an order).
    std::vector<Vec3> mergedVertices() const;
    // Approximate heap held, for the memory budget.
    std::size_t memoryBytes() const;

  private:
    struct Slot {
        Tile tile;
        std::vector<std::uint32_t> vertices; // merged vertex per tile vertex (topology only)
        std::vector<std::pair<std::int64_t, std::int64_t>> cells; // index cells it is listed in
        bool wide{}, used{};
    };
    struct Vertex {
        Vec3 point;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> incident; // (slot, triangle)
    };
    using CellKey = std::pair<std::int64_t, std::int64_t>;
    void index(std::uint32_t slot);
    void unindex(std::uint32_t slot);
    void link(std::uint32_t slot) const;
    void unlink(std::uint32_t slot) const;
    void buildTopology() const;
    std::optional<CellKey> cellOf(double x, double y) const;

    TerrainTileGrid grid_;
    std::map<TerrainTileKey, std::uint32_t> keys_;
    mutable std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
    std::map<CellKey, std::vector<std::uint32_t>> cells_;
    std::vector<std::uint32_t> wide_;
    std::size_t triangles_{};
    // Topology.
    mutable bool topologyBuilt_{};
    mutable std::vector<Vertex> vertices_;
    mutable std::vector<std::uint32_t> freeVertices_;
    mutable std::unordered_map<TerrainPositionKey, std::uint32_t, TerrainPositionHash> positions_;
};

// The edit mesh of a document terrain, cached per terrain and keyed by its tile BodyPtrs: a
// lookup with the same tiles returns the cached mesh, and a lookup after an edit updates it
// from the tiles the edit replaced rather than rebuilding it (in place when the cache holds the
// only reference, otherwise on a copy, so a mesh a caller holds never changes). Entries are
// keyed by document identity and terrain ID, and served only for the same tile records (the entry
// holds them, so their addresses cannot be reused).
// Thread-safe; at most `capacity` terrains (least recently used first out). With `topology`
// the topology is built before the mesh is returned, so concurrent readers never build it.
class TerrainEditMeshCache {
  public:
    explicit TerrainEditMeshCache(std::size_t capacity = 8) : capacity_(capacity) {}
    std::shared_ptr<const TerrainEditMesh> get(const Document &doc, Id terrain,
                                               bool topology = false);
    std::size_t size() const;
    std::size_t builds() const { return builds_; }
    std::size_t updates() const { return updates_; }
    void clear();
    // Drops the document's entries whose terrain no longer exists (the viewport calls it with
    // its other display caches), and forgets every entry of a document (by identity) that was
    // closed or replaced.
    void prune(const Document &doc);
    void forget(const std::string &documentIdentity);

  private:
    struct Entry {
        std::vector<BodyPtr> tiles; // ascending ID
        TerrainTileGrid grid;
        std::shared_ptr<const TerrainEditMesh> mesh;
        std::uint64_t used{};
    };
    mutable std::mutex mutex_;
    std::size_t capacity_;
    // Keyed by document identity and terrain: a copy of a document (a preview) shares its
    // identity and its tile records; another document never shares an entry.
    std::map<std::pair<std::string, Id>, Entry> entries_;
    std::uint64_t clock_{};
    std::size_t builds_{}, updates_{};
};
TerrainEditMeshCache &terrainEditMeshCache();
// A fresh edit mesh of a document terrain (not cached).
TerrainEditMesh terrainEditMesh(const Document &doc, Id terrain);
} // namespace sketchy
