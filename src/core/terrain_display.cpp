#include "core/terrain_display.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
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

TerrainContours terrainContours(const std::vector<const Body *> &tiles, double interval,
                                double elevationOffset) {
    if (!std::isfinite(interval) || interval < terrainContourIntervalMin ||
        interval > terrainContourIntervalMax)
        refuse("INVALID_REQUEST", "The contour interval is 0.01 to 1,000 m");
    if (!std::isfinite(elevationOffset))
        refuse("INVALID_REQUEST", "The elevation offset must be finite");
    TerrainContours result;
    result.interval = interval;
    result.elevationOffset = elevationOffset;
    // Pass 1: an upper bound of the segments (one per triangle and level it spans), so a dense
    // interval is refused in O(triangles) before anything is allocated.
    auto span = [&](double low, double high) {
        return std::floor(high / interval) - std::floor(low / interval);
    };
    double estimate = 0;
    for (const auto *tile : tiles)
        for (const auto &[id, face] : tile->surface.faces) {
            const auto &loop = face.loops.front();
            double low = std::numeric_limits<double>::infinity(), high = -low;
            for (const auto vertex : loop) {
                const auto e = tile->surface.vertices.at(vertex).z + elevationOffset;
                low = std::min(low, e);
                high = std::max(high, e);
            }
            estimate += span(low, high);
        }
    result.estimatedSegments = std::size_t(std::min(estimate, 1e18));
    if (estimate > double(terrainContourSegmentLimit)) {
        result.tooDense = true;
        return result;
    }
    result.segments.reserve(result.estimatedSegments);
    std::set<std::int64_t> levels;
    for (const auto *tile : tiles)
        for (const auto &[id, face] : tile->surface.faces) {
            const auto &loop = face.loops.front();
            std::array<Vec3, 3> p{};
            std::array<double, 3> e{};
            for (std::size_t i = 0; i < 3; ++i) {
                p[i] = tile->surface.vertices.at(loop[i]);
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
                    // Canonical endpoint order: the neighbour across this edge interpolates
                    // the same two points the same way.
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
                if (result.segments.size() >= terrainContourSegmentLimit) {
                    result.segments.clear();
                    result.segments.shrink_to_fit();
                    result.tooDense = true;
                    result.levels = result.indexLevels = 0;
                    return result;
                }
                const bool index = k % terrainContourIndexEvery == 0;
                result.segments.push_back({ends[0], ends[1], index});
                levels.insert(k);
            }
        }
    result.levels = levels.size();
    result.indexLevels = std::size_t(std::count_if(levels.begin(), levels.end(), [](auto k) {
        return k % terrainContourIndexEvery == 0;
    }));
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
    std::vector<BodyPtr> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(doc.bodies().at(id));
    auto &entry = entries_[terrain];
    entry.used = ++clock_;
    const bool sameTiles = entry.contours && entry.tiles == tiles;
    if (!sameTiles || entry.interval != interval || entry.offset != offset) {
        std::string digest = entry.digest;
        if (!sameTiles) {
            ++digests_;
            std::vector<const Body *> views;
            for (const auto &tile : tiles)
                views.push_back(tile.get());
            digest = terrainMeshDigest(views);
        }
        if (!entry.contours || digest != entry.digest || entry.interval != interval ||
            entry.offset != offset) {
            std::vector<const Body *> views;
            for (const auto &tile : tiles)
                views.push_back(tile.get());
            entry.contours =
                std::make_shared<const TerrainContours>(terrainContours(views, interval, offset));
            ++generations_;
        }
        entry.tiles = std::move(tiles);
        entry.digest = std::move(digest);
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
TerrainSurfaceDisplay terrainSurfaceDisplay(const std::vector<const Body *> &tiles) {
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
