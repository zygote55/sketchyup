#include "core/terrain_display.hpp"
#include "core/terrain_edit_mesh.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <iterator>
#include <tuple>
#include <unordered_set>
namespace sketchy {
namespace {
[[noreturn]] void refuse(const char *code, const std::string &message) {
    throw TerrainError(code, message);
}
bool lexicographicLess(Vec3 a, Vec3 b) {
    if (a.x != b.x)
        return a.x < b.x;
    if (a.y != b.y)
        return a.y < b.y;
    return a.z < b.z;
}
std::vector<const Body *> keyOrder(std::vector<const Body *> tiles) {
    std::stable_sort(tiles.begin(), tiles.end(),
                     [](const Body *a, const Body *b) { return a->tileKey < b->tileKey; });
    return tiles;
}
void checkInterval(double interval, double elevationOffset) {
    if (!std::isfinite(interval) || interval < terrainContourIntervalMin ||
        interval > terrainContourIntervalMax)
        refuse("INVALID_REQUEST", "The contour interval is 0.01 to 1,000 m");
    if (!std::isfinite(elevationOffset))
        refuse("INVALID_REQUEST", "The elevation offset must be finite");
}
// Pass 1 for one tile: an upper bound of its segments (one per triangle and level it spans).
// Each term is a whole number, so sums over tiles are exact in any grouping.
double tileEstimate(const Body &tile, double interval, double elevationOffset) {
    double estimate = 0;
    for (const auto &[id, face] : tile.surface.faces) {
        const auto &loop = face.loops.front();
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const auto vertex : loop) {
            const auto e = tile.surface.vertices.at(vertex).z + elevationOffset;
            low = std::min(low, e);
            high = std::max(high, e);
        }
        estimate += std::floor(high / interval) - std::floor(low / interval);
    }
    return estimate;
}
// Pass 2 for one tile: its segments in face order and the levels they are on.
void tileSegments(const Body &tile, double interval, double elevationOffset,
                  std::vector<TerrainContourSegment> &segments, std::set<std::int64_t> &levels) {
    for (const auto &[id, face] : tile.surface.faces) {
        const auto &loop = face.loops.front();
        std::array<Vec3, 3> p{};
        std::array<double, 3> e{};
        for (std::size_t i = 0; i < 3; ++i) {
            p[i] = tile.surface.vertices.at(loop[i]);
            e[i] = p[i].z + elevationOffset;
        }
        const double low = std::min({e[0], e[1], e[2]}), high = std::max({e[0], e[1], e[2]});
        const auto first = std::int64_t(std::floor(low / interval)) + 1,
                   last = std::int64_t(std::floor(high / interval));
        for (auto k = first; k <= last; ++k) {
            const double level = double(k) * interval;
            std::array<Vec3, 2> ends{};
            int found = 0;
            for (std::size_t i = 0; i < 3 && found < 2; ++i) {
                const auto j = (i + 1) % 3;
                if ((e[i] >= level) == (e[j] >= level))
                    continue;
                // Canonical endpoint order: the neighbour across this edge interpolates the
                // same two points the same way.
                auto a = p[i], b = p[j];
                auto ea = e[i], eb = e[j];
                if (lexicographicLess(b, a)) {
                    std::swap(a, b);
                    std::swap(ea, eb);
                }
                const double t = (level - ea) / (eb - ea);
                ends[std::size_t(found++)] = a + (b - a) * t;
            }
            // A level through one vertex only touches the triangle: no segment.
            if (found != 2 || ends[0] == ends[1])
                continue;
            segments.push_back({ends[0], ends[1], k % terrainContourIndexEvery == 0});
            levels.insert(k);
        }
    }
}
std::size_t indexLevels(const std::set<std::int64_t> &levels) {
    return std::size_t(std::count_if(levels.begin(), levels.end(), [](auto k) {
        return k % terrainContourIndexEvery == 0;
    }));
}
} // namespace

ChangeReport setTerrainDisplay(Document &doc, Id terrain, const TerrainDisplay &display) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || found->second->kind != BodyKind::Terrain ||
        !found->second->terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    for (Id id = terrain; id; id = doc.bodies().at(id)->parent)
        if (doc.bodies().at(id)->locked)
            refuse("INVALID_OPERATION", "Cannot edit a locked entity or its contents");
    validateTerrainDisplay(display);
    if (found->second->terrain->display == display)
        return {};
    auto body = std::make_shared<Body>(*found->second);
    body->terrain->display = display;
    return doc.apply({"Terrain display", {{terrain, found->second, body}}}, doc.revision());
}

TerrainContours terrainContours(const std::vector<const Body *> &input, double interval,
                                double elevationOffset) {
    checkInterval(interval, elevationOffset);
    const auto tiles = keyOrder(input);
    TerrainContours result;
    result.interval = interval;
    result.elevationOffset = elevationOffset;
    // Pass 1: an upper bound of the segments, so a dense interval is refused in O(triangles)
    // before anything is allocated.
    double estimate = 0;
    for (const auto *tile : tiles)
        estimate += tileEstimate(*tile, interval, elevationOffset);
    result.estimatedSegments = std::size_t(std::min(estimate, 1e18));
    if (estimate > double(terrainContourSegmentLimit)) {
        result.tooDense = true;
        return result;
    }
    // Pass 2. Segments never outnumber the estimate, so the limit holds.
    result.segments.reserve(result.estimatedSegments);
    std::set<std::int64_t> levels;
    for (const auto *tile : tiles)
        tileSegments(*tile, interval, elevationOffset, result.segments, levels);
    result.levels = levels.size();
    result.indexLevels = indexLevels(levels);
    return result;
}
double terrainElevationOffset(const Document &doc, Id terrain) {
    const auto world = doc.worldTransform(terrain);
    return world.m[14] + (doc.site() ? doc.site()->elevation : 0.0);
}

std::shared_ptr<const TerrainContours> TerrainContourCache::get(const Document &doc, Id terrain) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || !found->second->terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    const auto interval = found->second->terrain->display.contourInterval;
    const auto offset = terrainElevationOffset(doc, terrain);
    checkInterval(interval, offset);
    std::vector<BodyPtr> tiles;
    for (const auto id : terrainTilesInKeyOrder(doc, terrain))
        tiles.push_back(doc.bodies().at(id));
    auto &entry = entries_[terrain];
    entry.used = ++clock_;
    if (!entry.contours || entry.tiles != tiles || entry.interval != interval ||
        entry.offset != offset) {
        if (entry.interval != interval || entry.offset != offset)
            entry.perTile.clear();
        // Each tile's contours: kept for the same record, or the same mesh (paint changes the
        // record only); generated otherwise.
        std::map<Id, std::shared_ptr<const Tile>> perTile;
        bool reusedAll = entry.contours && entry.interval == interval && entry.offset == offset &&
                         entry.tiles.size() == tiles.size();
        double estimate = 0;
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const auto &body = tiles[i];
            std::shared_ptr<const Tile> tile;
            if (const auto old = entry.perTile.find(body->id); old != entry.perTile.end() &&
                (old->second->body == body ||
                 (old->second->body->surface.vertices == body->surface.vertices &&
                  old->second->body->surface.faces == body->surface.faces)))
                tile = old->second;
            else {
                auto fresh = std::make_shared<Tile>();
                fresh->body = body;
                fresh->estimate = tileEstimate(*body, interval, offset);
                if (fresh->estimate <= double(terrainContourSegmentLimit)) {
                    std::set<std::int64_t> levels;
                    tileSegments(*body, interval, offset, fresh->segments, levels);
                    fresh->levels.assign(levels.begin(), levels.end());
                }
                tile = std::move(fresh);
                ++tileGenerations_;
            }
            reusedAll = reusedAll && entry.tiles[i]->id == body->id &&
                        entry.perTile.contains(body->id) && entry.perTile.at(body->id) == tile;
            estimate += tile->estimate;
            perTile[body->id] = std::move(tile);
        }
        if (!reusedAll) {
            auto contours = std::make_shared<TerrainContours>();
            contours->interval = interval;
            contours->elevationOffset = offset;
            contours->estimatedSegments = std::size_t(std::min(estimate, 1e18));
            if (estimate > double(terrainContourSegmentLimit))
                contours->tooDense = true;
            else {
                std::set<std::int64_t> levels;
                contours->segments.reserve(contours->estimatedSegments);
                for (const auto &body : tiles) {
                    const auto &tile = *perTile.at(body->id);
                    contours->segments.insert(contours->segments.end(), tile.segments.begin(),
                                              tile.segments.end());
                    levels.insert(tile.levels.begin(), tile.levels.end());
                }
                contours->levels = levels.size();
                contours->indexLevels = indexLevels(levels);
            }
            entry.contours = std::move(contours);
            ++generations_;
        }
        entry.perTile = std::move(perTile);
        entry.tiles = std::move(tiles);
        entry.interval = interval;
        entry.offset = offset;
    }
    auto result = entry.contours;
    while (entries_.size() > capacity_) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->first != terrain && (oldest == entries_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == entries_.end())
            break;
        entries_.erase(oldest);
    }
    return result;
}
void TerrainContourCache::prune(const Document &doc) {
    std::erase_if(entries_, [&](const auto &item) { return !isTerrain(doc, item.first); });
}

std::size_t TerrainSurfaceDisplay::Hash::operator()(const Key &key) const {
    std::uint64_t h = key.x * 0x9E3779B97F4A7C15ULL;
    h ^= key.y + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    h ^= key.z + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return std::size_t(h);
}
TerrainSurfaceDisplay::Key TerrainSurfaceDisplay::key(Vec3 point) {
    // +0.0 and -0.0 are the same position.
    auto bits = [](double v) { return std::bit_cast<std::uint64_t>(v == 0 ? 0.0 : v); };
    return {bits(point.x), bits(point.y), bits(point.z)};
}
Vec3 TerrainSurfaceDisplay::normal(Vec3 point, Vec3 fallback) const {
    const auto found = normals.find(key(point));
    return found == normals.end() ? fallback : found->second;
}
bool TerrainSurfaceDisplay::outerEdge(Id tile, Id edge) const {
    const auto found = outerEdges.find(tile);
    return found != outerEdges.end() && found->second.contains(edge);
}
TerrainSurfaceDisplay terrainSurfaceDisplay(const std::vector<const Body *> &input) {
    const auto tiles = keyOrder(input);
    TerrainSurfaceDisplay result;
    result.zMin = std::numeric_limits<double>::infinity();
    result.zMax = -result.zMin;
    std::size_t vertices = 0;
    for (const auto *tile : tiles)
        vertices += tile->surface.vertices.size();
    result.normals.reserve(vertices);
    for (const auto *tile : tiles) {
        const auto &surface = tile->surface;
        for (const auto &[id, point] : surface.vertices) {
            result.zMin = std::min(result.zMin, point.z);
            result.zMax = std::max(result.zMax, point.z);
        }
        for (const auto &[id, face] : surface.faces) {
            const auto &loop = face.loops.front();
            const auto a = surface.vertices.at(loop[0]), b = surface.vertices.at(loop[1]),
                       c = surface.vertices.at(loop[2]);
            // Twice the area times the unit normal: area weighting, as ShadingNormals.
            const auto weighted = cross(b - a, c - a);
            for (const auto point : {a, b, c}) {
                auto &sum = result.normals[TerrainSurfaceDisplay::key(point)];
                sum = sum + weighted;
            }
        }
    }
    for (auto &[key, sum] : result.normals) {
        const auto magnitude = length(sum);
        sum = magnitude > 0 ? sum * (1 / magnitude) : Vec3{0, 0, 1};
    }
    // A tile's interior edges (two faces) carry the implied soft and smooth appearance; the
    // rest are on its border. A border edge whose positions appear in another tile's border is
    // a seam; one that appears once is on the terrain's outer boundary.
    struct Use {
        Id tile{}, edge{};
        int count{};
    };
    struct PairHash {
        std::size_t operator()(const std::pair<TerrainSurfaceDisplay::Key,
                                               TerrainSurfaceDisplay::Key> &pair) const {
            const TerrainSurfaceDisplay::Hash hash;
            return hash(pair.first) * 31 + hash(pair.second);
        }
    };
    std::unordered_map<std::pair<TerrainSurfaceDisplay::Key, TerrainSurfaceDisplay::Key>, Use,
                       PairHash>
        border;
    for (const auto *tile : tiles)
        for (const auto &[id, edge] : tile->topology.edges) {
            if (tile->edgeAppearances.contains(id))
                continue;
            auto a = TerrainSurfaceDisplay::key(tile->surface.vertices.at(edge.a)),
                 b = TerrainSurfaceDisplay::key(tile->surface.vertices.at(edge.b));
            if (std::tie(b.x, b.y, b.z) < std::tie(a.x, a.y, a.z))
                std::swap(a, b);
            auto &use = border[{a, b}];
            if (!use.count++)
                use = {tile->id, id, 1};
        }
    for (const auto &[pair, use] : border)
        if (use.count == 1)
            result.outerEdges[use.tile].insert(use.edge);
    if (tiles.empty())
        result.zMin = result.zMax = 0;
    return result;
}

// ---- R096.a per-tile surfaces ---------------------------------------------------------------
Vec3 TerrainTileSurface::normal(Vec3 point, Vec3 fallback) const {
    const auto found = normals.find(TerrainSurfaceDisplay::key(point));
    return found == normals.end() ? fallback : found->second;
}
namespace {
// One tile's normals and outer edges from the edit mesh: each position sums the weighted normals
// of its triangles in (tile key, index) order, as terrainSurfaceDisplay does in key order.
std::shared_ptr<const TerrainTileSurface> tileSurface(const Body &tile,
                                                      const TerrainEditMesh &mesh) {
    auto surface = std::make_shared<TerrainTileSurface>();
    auto corners = [&](const TerrainTriangleRef &ref) {
        const auto &content = *mesh.tile(ref.tile)->content;
        const auto &t = content.triangles[ref.triangle];
        return std::array<Vec3, 3>{content.vertices[t[0]], content.vertices[t[1]],
                                   content.vertices[t[2]]};
    };
    surface->normals.reserve(tile.surface.vertices.size());
    for (const auto &[id, point] : tile.surface.vertices) {
        Vec3 sum{};
        for (const auto &ref : mesh.trianglesAt(point)) {
            const auto [a, b, c] = corners(ref);
            sum = sum + cross(b - a, c - a);
        }
        const auto magnitude = length(sum);
        surface->normals[TerrainSurfaceDisplay::key(point)] =
            magnitude > 0 ? sum * (1 / magnitude) : Vec3{0, 0, 1};
    }
    for (const auto &[id, edge] : tile.topology.edges) {
        if (tile.edgeAppearances.contains(id))
            continue; // an interior edge of the tile (two faces)
        const auto a = mesh.trianglesAt(tile.surface.vertices.at(edge.a)),
                   b = mesh.trianglesAt(tile.surface.vertices.at(edge.b));
        std::vector<TerrainTriangleRef> both;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(both));
        if (both.size() == 1)
            surface->outerEdges.insert(id);
    }
    return surface;
}
} // namespace
std::shared_ptr<const TerrainTileSurfaces> TerrainSurfaceCache::get(const Document &doc,
                                                                   Id terrain) {
    if (!isTerrain(doc, terrain))
        refuse("NOT_FOUND", "Terrain body does not exist");
    std::vector<BodyPtr> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(doc.bodies().at(id));
    auto &entry = entries_[terrain];
    entry.used = ++clock_;
    if (!entry.surfaces || entry.tiles != tiles) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        auto surfaces = std::make_shared<TerrainTileSurfaces>();
        // Positions whose triangles changed: those of tiles that left or entered (a record with
        // the same ID and mesh, such as a paint change, keeps its surface).
        std::map<Id, BodyPtr> before;
        if (entry.surfaces)
            for (const auto &tile : entry.tiles)
                before[tile->id] = tile;
        auto sameMesh = [](const Body &a, const Body &b) {
            return a.surface.vertices == b.surface.vertices && a.surface.faces == b.surface.faces &&
                   a.topology == b.topology && a.edgeAppearances == b.edgeAppearances;
        };
        std::unordered_set<TerrainSurfaceDisplay::Key, TerrainSurfaceDisplay::Hash> changed;
        std::set<Id> current;
        for (const auto &tile : tiles) {
            current.insert(tile->id);
            const auto old = before.find(tile->id);
            if (old != before.end() && (old->second == tile || sameMesh(*old->second, *tile)))
                continue;
            for (const auto &[id, p] : tile->surface.vertices)
                changed.insert(TerrainSurfaceDisplay::key(p));
        }
        for (const auto &[id, tile] : before)
            if (!current.contains(id))
                for (const auto &[vertex, p] : tile->surface.vertices)
                    changed.insert(TerrainSurfaceDisplay::key(p));
        for (const auto &tile : tiles) {
            std::shared_ptr<const TerrainTileSurface> kept;
            if (entry.surfaces)
                if (const auto old = before.find(tile->id);
                    old != before.end() && (old->second == tile || sameMesh(*old->second, *tile))) {
                    bool touched = false;
                    for (const auto &[id, p] : tile->surface.vertices)
                        if (changed.contains(TerrainSurfaceDisplay::key(p))) {
                            touched = true;
                            break;
                        }
                    if (!touched)
                        kept = entry.surfaces->tile(tile->id);
                }
            if (!kept) {
                kept = tileSurface(*tile, *mesh);
                ++computations_;
            }
            surfaces->tiles[tile->id] = std::move(kept);
        }
        surfaces->zMin = std::numeric_limits<double>::infinity();
        surfaces->zMax = -surfaces->zMin;
        for (const auto *tile : mesh->tiles()) {
            surfaces->zMin = std::min(surfaces->zMin, tile->minZ);
            surfaces->zMax = std::max(surfaces->zMax, tile->maxZ);
        }
        if (tiles.empty())
            surfaces->zMin = surfaces->zMax = 0;
        entry.surfaces = std::move(surfaces);
        entry.tiles = std::move(tiles);
    }
    auto result = entry.surfaces;
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
void TerrainSurfaceCache::prune(const Document &doc) {
    std::erase_if(entries_, [&](const auto &item) { return !isTerrain(doc, item.first); });
}

std::array<float, 3> terrainElevationBandColor(double z, double zMin, double zMax) {
    // Low green, middle sand, high brown (the import preview's hypsometric ramp).
    static constexpr std::array<std::array<float, 3>, 3> ramp{
        {{0x4f / 255.f, 0x8a / 255.f, 0x5b / 255.f},
         {0xc9 / 255.f, 0xb5 / 255.f, 0x6e / 255.f},
         {0x8c / 255.f, 0x5a / 255.f, 0x3c / 255.f}}};
    const double range = zMax - zMin;
    double t = range > 0 ? (z - zMin) / range : .5;
    t = std::clamp(t, 0.0, 1.0);
    t = std::min(9.0, std::floor(t * 10)) / 9.0; // Ten bands.
    const auto &a = t < .5 ? ramp[0] : ramp[1];
    const auto &b = t < .5 ? ramp[1] : ramp[2];
    const float u = float(t < .5 ? t * 2 : (t - .5) * 2);
    return {a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u};
}
} // namespace sketchy
