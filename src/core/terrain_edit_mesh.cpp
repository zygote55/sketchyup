#include "core/terrain_edit_mesh.hpp"
#include "core/terrain.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
namespace sketchy {
namespace {
// Tiles whose bounding box spans more cells than this per axis are listed once, as wide tiles,
// instead of in every cell (long thin triangles in contour terrains).
constexpr std::int64_t wideCells = 8;
// Index padding: wider than any slack a query uses (terrainElevationAt uses 1e-9 m for tile
// boxes and 1e-7 m for triangles).
constexpr double indexPadding = 1e-6;
constexpr double cellIndexLimit = 4.0e18;
} // namespace

TerrainPositionKey TerrainPositionKey::of(Vec3 point) {
    auto bits = [](double v) { return std::bit_cast<std::uint64_t>(v == 0 ? 0.0 : v); };
    return {bits(point.x), bits(point.y), bits(point.z)};
}
std::size_t TerrainPositionHash::operator()(const TerrainPositionKey &key) const {
    std::uint64_t h = key.x * 0x9E3779B97F4A7C15ULL;
    h ^= key.y + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    h ^= key.z + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return std::size_t(h);
}
TerrainTilePtr terrainTileContent(const Body &tile) {
    auto content = std::make_shared<TerrainTileContent>();
    content->key = tile.tileKey;
    content->name = tile.name;
    auto mesh = terrainTileMesh(tile);
    content->vertices = std::move(mesh.vertices);
    content->triangles = std::move(mesh.triangles);
    return content;
}

std::optional<TerrainEditMesh::CellKey> TerrainEditMesh::cellOf(double x, double y) const {
    if (!(grid_.cell > 0))
        return std::nullopt;
    const double u = std::floor((x - grid_.originX) / grid_.cell),
                 v = std::floor((y - grid_.originY) / grid_.cell);
    if (!std::isfinite(u) || !std::isfinite(v) || std::abs(u) > cellIndexLimit ||
        std::abs(v) > cellIndexLimit)
        return std::nullopt;
    return CellKey{std::int64_t(v), std::int64_t(u)};
}
void TerrainEditMesh::index(std::uint32_t slot) {
    auto &s = slots_[slot];
    const auto low = cellOf(s.tile.minX - indexPadding, s.tile.minY - indexPadding),
               high = cellOf(s.tile.maxX + indexPadding, s.tile.maxY + indexPadding);
    s.cells.clear();
    s.wide = !low || !high || high->first - low->first >= wideCells ||
             high->second - low->second >= wideCells;
    if (s.wide) {
        wide_.push_back(slot);
        return;
    }
    for (auto row = low->first; row <= high->first; ++row)
        for (auto column = low->second; column <= high->second; ++column) {
            cells_[{row, column}].push_back(slot);
            s.cells.push_back({row, column});
        }
}
void TerrainEditMesh::unindex(std::uint32_t slot) {
    auto &s = slots_[slot];
    if (s.wide)
        std::erase(wide_, slot);
    for (const auto &cell : s.cells) {
        auto found = cells_.find(cell);
        if (found == cells_.end())
            continue;
        std::erase(found->second, slot);
        if (found->second.empty())
            cells_.erase(found);
    }
    s.cells.clear();
    s.wide = false;
}
void TerrainEditMesh::insertTile(TerrainTilePtr content, Id body) {
    if (!content)
        throw std::invalid_argument("A terrain tile needs its content");
    if (keys_.contains(content->key))
        eraseTile(content->key);
    std::uint32_t slot;
    if (!freeSlots_.empty()) {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        slot = std::uint32_t(slots_.size());
        slots_.emplace_back();
    }
    auto &s = slots_[slot];
    s = Slot{};
    s.used = true;
    s.tile.content = std::move(content);
    s.tile.body = body;
    const auto &vertices = s.tile.content->vertices;
    s.tile.minX = s.tile.minY = s.tile.minZ = std::numeric_limits<double>::infinity();
    s.tile.maxX = s.tile.maxY = s.tile.maxZ = -std::numeric_limits<double>::infinity();
    for (const auto &p : vertices) {
        s.tile.minX = std::min(s.tile.minX, p.x);
        s.tile.maxX = std::max(s.tile.maxX, p.x);
        s.tile.minY = std::min(s.tile.minY, p.y);
        s.tile.maxY = std::max(s.tile.maxY, p.y);
        s.tile.minZ = std::min(s.tile.minZ, p.z);
        s.tile.maxZ = std::max(s.tile.maxZ, p.z);
    }
    keys_[s.tile.content->key] = slot;
    triangles_ += s.tile.content->triangles.size();
    index(slot);
    if (topologyBuilt_)
        link(slot);
}
void TerrainEditMesh::eraseTile(const TerrainTileKey &key) {
    const auto found = keys_.find(key);
    if (found == keys_.end())
        return;
    const auto slot = found->second;
    keys_.erase(found);
    if (topologyBuilt_)
        unlink(slot);
    unindex(slot);
    triangles_ -= slots_[slot].tile.content->triangles.size();
    slots_[slot] = Slot{};
    freeSlots_.push_back(slot);
}
const TerrainEditMesh::Tile *TerrainEditMesh::tile(const TerrainTileKey &key) const {
    const auto found = keys_.find(key);
    return found == keys_.end() ? nullptr : &slots_[found->second].tile;
}
std::vector<const TerrainEditMesh::Tile *> TerrainEditMesh::tiles() const {
    std::vector<const Tile *> result;
    result.reserve(keys_.size());
    for (const auto &[key, slot] : keys_)
        result.push_back(&slots_[slot].tile);
    return result;
}
std::vector<const TerrainEditMesh::Tile *> TerrainEditMesh::cellTiles(std::int64_t row,
                                                                       std::int64_t column) const {
    std::vector<const Tile *> result;
    for (auto it = keys_.lower_bound(TerrainTileKey{row, column, {}, 0});
         it != keys_.end() && it->first.row == row && it->first.column == column; ++it)
        result.push_back(&slots_[it->second].tile);
    return result;
}
std::vector<const TerrainEditMesh::Tile *> TerrainEditMesh::tilesAt(double x, double y,
                                                                     double slack) const {
    std::vector<std::uint32_t> candidates = wide_;
    if (const auto cell = cellOf(x, y))
        if (const auto found = cells_.find(*cell); found != cells_.end())
            candidates.insert(candidates.end(), found->second.begin(), found->second.end());
    std::vector<const Tile *> result;
    for (const auto slot : candidates) {
        const auto &t = slots_[slot].tile;
        if (x < t.minX - slack || x > t.maxX + slack || y < t.minY - slack || y > t.maxY + slack)
            continue;
        result.push_back(&t);
    }
    std::sort(result.begin(), result.end(),
              [](const Tile *a, const Tile *b) { return a->content->key < b->content->key; });
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void TerrainEditMesh::link(std::uint32_t slot) const {
    auto &s = slots_[slot];
    const auto &content = *s.tile.content;
    s.vertices.resize(content.vertices.size());
    for (std::size_t i = 0; i < content.vertices.size(); ++i) {
        const auto key = TerrainPositionKey::of(content.vertices[i]);
        auto [it, inserted] = positions_.try_emplace(key, 0);
        if (inserted) {
            std::uint32_t index;
            if (!freeVertices_.empty()) {
                index = freeVertices_.back();
                freeVertices_.pop_back();
                vertices_[index] = Vertex{content.vertices[i], {}};
            } else {
                index = std::uint32_t(vertices_.size());
                vertices_.push_back(Vertex{content.vertices[i], {}});
            }
            it->second = index;
        }
        s.vertices[i] = it->second;
    }
    for (std::uint32_t t = 0; t < content.triangles.size(); ++t)
        for (const auto corner : content.triangles[t])
            vertices_[s.vertices[corner]].incident.push_back({slot, t});
}
void TerrainEditMesh::unlink(std::uint32_t slot) const {
    auto &s = slots_[slot];
    // Each merged vertex of the tile loses this tile's incidences once.
    std::vector<std::uint32_t> merged = s.vertices;
    std::sort(merged.begin(), merged.end());
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
    for (const auto index : merged) {
        auto &vertex = vertices_[index];
        std::erase_if(vertex.incident, [&](const auto &use) { return use.first == slot; });
        if (vertex.incident.empty()) {
            positions_.erase(TerrainPositionKey::of(vertex.point));
            vertex = Vertex{};
            freeVertices_.push_back(index);
        }
    }
    s.vertices.clear();
}
void TerrainEditMesh::buildTopology() const {
    if (topologyBuilt_)
        return;
    positions_.reserve(triangles_);
    vertices_.reserve(triangles_ / 2 + 16);
    for (const auto &[key, slot] : keys_)
        link(slot);
    topologyBuilt_ = true;
}
std::vector<TerrainTriangleRef> TerrainEditMesh::trianglesAt(Vec3 point) const {
    buildTopology();
    std::vector<TerrainTriangleRef> result;
    const auto found = positions_.find(TerrainPositionKey::of(point));
    if (found == positions_.end())
        return result;
    for (const auto &[slot, triangle] : vertices_[found->second].incident)
        result.push_back({slots_[slot].tile.content->key, triangle});
    std::sort(result.begin(), result.end());
    return result;
}
std::size_t TerrainEditMesh::mergedVertexCount() const {
    buildTopology();
    return positions_.size();
}
std::vector<Vec3> TerrainEditMesh::mergedVertices() const {
    buildTopology();
    std::vector<Vec3> result;
    result.reserve(positions_.size());
    for (const auto &[key, index] : positions_)
        result.push_back(vertices_[index].point);
    return result;
}
std::size_t TerrainEditMesh::memoryBytes() const {
    std::size_t bytes = sizeof(*this) + slots_.capacity() * sizeof(Slot) +
                        keys_.size() * (sizeof(TerrainTileKey) + 48) +
                        wide_.capacity() * sizeof(std::uint32_t);
    for (const auto &[cell, list] : cells_)
        bytes += 64 + list.capacity() * sizeof(std::uint32_t);
    for (const auto &slot : slots_)
        bytes += slot.vertices.capacity() * sizeof(std::uint32_t) +
                 slot.cells.capacity() * sizeof(CellKey);
    bytes += vertices_.capacity() * sizeof(Vertex) + freeVertices_.capacity() * 4 +
             positions_.size() * (sizeof(TerrainPositionKey) + 4 + 16) +
             positions_.bucket_count() * sizeof(void *);
    for (const auto &vertex : vertices_)
        bytes += vertex.incident.capacity() * sizeof(vertex.incident[0]);
    return bytes;
}

// ---- Cache ------------------------------------------------------------------------------
namespace {
std::vector<BodyPtr> tileBodies(const Document &doc, Id terrain) {
    std::vector<BodyPtr> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(doc.bodies().at(id));
    return tiles;
}
} // namespace
TerrainEditMesh terrainEditMesh(const Document &doc, Id terrain) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || !found->second->terrain)
        throw TerrainError("NOT_FOUND", "Terrain body does not exist");
    TerrainEditMesh mesh(found->second->terrain->tiling);
    for (const auto &tile : tileBodies(doc, terrain))
        mesh.insertTile(terrainTileContent(*tile), tile->id);
    return mesh;
}
std::shared_ptr<const TerrainEditMesh> TerrainEditMeshCache::get(const Document &doc, Id terrain,
                                                                bool topology) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || !found->second->terrain)
        throw TerrainError("NOT_FOUND", "Terrain body does not exist");
    const auto grid = found->second->terrain->tiling;
    auto tiles = tileBodies(doc, terrain);
    std::lock_guard lock(mutex_);
    auto &entry = entries_[terrain];
    entry.used = ++clock_;
    if (!entry.mesh || entry.grid != grid) {
        auto mesh = std::make_shared<TerrainEditMesh>(grid);
        for (const auto &tile : tiles)
            mesh->insertTile(terrainTileContent(*tile), tile->id);
        entry.mesh = std::move(mesh);
        entry.tiles = std::move(tiles);
        entry.grid = grid;
        ++builds_;
    } else if (entry.tiles != tiles) {
        // The tiles an edit replaced: those only in the old list leave, those only in the new
        // one enter (both lists ascend by ID; a changed body with a kept ID is in both).
        std::shared_ptr<TerrainEditMesh> mesh;
        if (entry.mesh.use_count() == 1)
            mesh = std::const_pointer_cast<TerrainEditMesh>(entry.mesh);
        else
            mesh = std::make_shared<TerrainEditMesh>(*entry.mesh);
        std::vector<BodyPtr> gone, added;
        // Set differences by identity (sorted copies by pointer).
        auto byPointer = [](std::vector<BodyPtr> list) {
            std::sort(list.begin(), list.end(),
                      [](const BodyPtr &a, const BodyPtr &b) { return a.get() < b.get(); });
            return list;
        };
        const auto before = byPointer(entry.tiles), after = byPointer(tiles);
        std::set_difference(before.begin(), before.end(), after.begin(), after.end(),
                            std::back_inserter(gone),
                            [](const BodyPtr &a, const BodyPtr &b) { return a.get() < b.get(); });
        std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                            std::back_inserter(added),
                            [](const BodyPtr &a, const BodyPtr &b) { return a.get() < b.get(); });
        for (const auto &tile : gone)
            if (const auto *current = mesh->tile(tile->tileKey); current && current->body == tile->id)
                mesh->eraseTile(tile->tileKey);
        for (const auto &tile : added)
            mesh->insertTile(terrainTileContent(*tile), tile->id);
        entry.mesh = std::move(mesh);
        entry.tiles = std::move(tiles);
        ++updates_;
    }
    if (topology && !entry.mesh->topologyBuilt()) {
        // Build on a mesh nobody else holds, so readers never see it change.
        std::shared_ptr<TerrainEditMesh> mesh;
        if (entry.mesh.use_count() == 1)
            mesh = std::const_pointer_cast<TerrainEditMesh>(entry.mesh);
        else
            mesh = std::make_shared<TerrainEditMesh>(*entry.mesh);
        mesh->mergedVertexCount();
        entry.mesh = std::move(mesh);
    }
    auto result = entry.mesh;
    while (entries_.size() > capacity_) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->first != terrain &&
                (oldest == entries_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == entries_.end())
            break;
        entries_.erase(oldest);
    }
    return result;
}
std::size_t TerrainEditMeshCache::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}
void TerrainEditMeshCache::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
}
TerrainEditMeshCache &terrainEditMeshCache() {
    static TerrainEditMeshCache cache;
    return cache;
}
} // namespace sketchy
