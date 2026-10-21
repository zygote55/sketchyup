#include "core/terrain_edit.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_sources.hpp"
#include "core/component_parameter_records.hpp"
#include "core/document_limits.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
namespace sketchy {
namespace {
[[noreturn]] void refuse(const char *code, const std::string &message) {
    throw TerrainError(code, message);
}
using PlanTriangle = std::array<Vec3, 3>;
// Above this, a cell or sub-cell index is not representable (refused, never wrapped).
constexpr double cellLimit = 4.0e18;
Vec3 centroidOf(const PlanTriangle &t) {
    // The same expression as tileTerrainTriangles, so a triangle lands where creation put it.
    return {(t[0].x + t[1].x + t[2].x) / 3, (t[0].y + t[1].y + t[2].y) / 3, 0};
}
TerrainTilePtr buildContent(const TerrainTileKey &key, std::string name,
                            const std::vector<PlanTriangle> &triangles) {
    auto content = std::make_shared<TerrainTileContent>();
    content->key = key;
    content->name = std::move(name);
    std::unordered_map<TerrainPositionKey, std::uint32_t, TerrainPositionHash> local;
    local.reserve(triangles.size() * 2);
    content->triangles.reserve(triangles.size());
    for (const auto &triangle : triangles) {
        std::array<std::uint32_t, 3> indices{};
        for (int corner = 0; corner < 3; ++corner) {
            const auto [it, inserted] = local.try_emplace(TerrainPositionKey::of(triangle[corner]),
                                                          std::uint32_t(content->vertices.size()));
            if (inserted)
                content->vertices.push_back(triangle[corner]);
            indices[corner] = it->second;
        }
        content->triangles.push_back(indices);
    }
    return content;
}
void checkPosition(Vec3 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        std::abs(p.x) > coordinateLimit || std::abs(p.y) > coordinateLimit ||
        std::abs(p.z) > coordinateLimit)
        refuse("INVALID_REQUEST", "Terrain edit positions must be finite and within the "
                                  "coordinate limit");
}
} // namespace

// ---- Stable tiling -------------------------------------------------------------------------
std::optional<std::array<std::int64_t, 2>> terrainCellOf(const TerrainTileGrid &grid, double x,
                                                         double y) {
    if (!(grid.cell > 0))
        return std::nullopt;
    const double column = std::floor((x - grid.originX) / grid.cell),
                 row = std::floor((y - grid.originY) / grid.cell);
    if (!std::isfinite(row) || !std::isfinite(column) || std::abs(row) > cellLimit ||
        std::abs(column) > cellLimit || std::abs(row) > double(terrainTileKeyLimit) ||
        std::abs(column) > double(terrainTileKeyLimit))
        return std::nullopt;
    return std::array<std::int64_t, 2>{std::int64_t(row), std::int64_t(column)};
}
int terrainQuadrantDigit(const TerrainTileGrid &grid, double x, double y, std::size_t depth) {
    // (x - origin) / cell is the expression of the cell index; scaling it by 2^depth is exact,
    // so the sub-cell index at each depth refines the one above it.
    auto bit = [&](double offset) {
        const double scaled = std::floor(std::ldexp(offset / grid.cell, int(depth)));
        if (!std::isfinite(scaled) || std::abs(scaled) > cellLimit)
            refuse("INVALID_REQUEST", "Terrain edit lies outside the representable tile grid");
        const auto index = std::int64_t(scaled);
        return int(((index % 2) + 2) % 2);
    };
    return 2 * bit(y - grid.originY) + bit(x - grid.originX);
}
std::string terrainTileKeyName(const TerrainTileKey &key) {
    std::string name =
        "Terrain tile R" + std::to_string(key.row) + " C" + std::to_string(key.column);
    if (!key.path.empty())
        name += " Q" + key.path;
    if (key.part)
        name += " part " + std::to_string(key.part + 1);
    return name;
}
TerrainTileSet terrainTileSet(const TerrainMesh &mesh, TerrainProgress *progress) {
    const auto tiling = tileTerrainTriangles(mesh, progress);
    TerrainTileSet set;
    set.grid = {tiling.originX, tiling.originY, tiling.cell};
    set.tiles.reserve(tiling.tiles.size());
    for (std::size_t t = 0; t < tiling.tiles.size(); ++t) {
        std::vector<PlanTriangle> triangles;
        triangles.reserve(tiling.tiles[t].size());
        for (const auto index : tiling.tiles[t]) {
            const auto &tri = mesh.triangles[index];
            triangles.push_back({mesh.vertices[tri[0]], mesh.vertices[tri[1]], mesh.vertices[tri[2]]});
        }
        set.tiles.push_back(
            buildContent(tiling.keys[t], "Terrain tile " + std::to_string(t + 1), triangles));
    }
    return set;
}

// ---- Local tile replacement ----------------------------------------------------------------
namespace {
using Leaf = std::tuple<std::int64_t, std::int64_t, std::string>;
Leaf leafOf(const TerrainTileKey &key) { return {key.row, key.column, key.path}; }
void retile(const TerrainTileGrid &grid, std::int64_t row, std::int64_t column, std::string path,
            std::vector<PlanTriangle> triangles, std::vector<std::pair<TerrainTileKey, std::vector<PlanTriangle>>> &out) {
    constexpr auto limit = TerrainLimits::tileTriangles;
    if (triangles.empty())
        return;
    if (triangles.size() <= limit) {
        out.push_back({{row, column, path, 0}, std::move(triangles)});
        return;
    }
    if (path.size() >= terrainTileMaxDepth) {
        // A cluster no split separates: parts in triangle order.
        for (std::size_t start = 0, part = 0; start < triangles.size(); start += limit, ++part)
            out.push_back({{row, column, path, std::uint32_t(part)},
                           std::vector<PlanTriangle>(triangles.begin() + std::ptrdiff_t(start),
                                                 triangles.begin() + std::ptrdiff_t(std::min(
                                                                         triangles.size(), start + limit)))});
        return;
    }
    std::array<std::vector<PlanTriangle>, 4> quadrants;
    for (auto &triangle : triangles) {
        const auto c = centroidOf(triangle);
        quadrants[std::size_t(terrainQuadrantDigit(grid, c.x, c.y, path.size() + 1))].push_back(
            std::move(triangle));
    }
    for (int q = 0; q < 4; ++q)
        retile(grid, row, column, path + char('0' + q), std::move(quadrants[std::size_t(q)]), out);
}
} // namespace
TerrainRegionResult replaceTerrainRegion(const TerrainEditMesh &mesh,
                                         const TerrainRegionChange &change) {
    const auto &grid = mesh.grid();
    if (!(grid.cell > 0))
        refuse("INVALID_REQUEST", "The terrain has no stored tile grid");
    std::set<Leaf> touched;
    std::set<TerrainTriangleRef> removed;
    for (const auto &ref : change.removed) {
        const auto *tile = mesh.tile(ref.tile);
        if (!tile || ref.triangle >= tile->content->triangles.size())
            refuse("INVALID_REQUEST", "A removed terrain triangle does not exist");
        if (!removed.insert(ref).second)
            refuse("INVALID_REQUEST", "A terrain triangle is removed twice");
        touched.insert(leafOf(ref.tile));
    }
    std::unordered_map<TerrainPositionKey, double, TerrainPositionHash> lifts;
    for (const auto &[point, z] : change.lifted) {
        checkPosition(point);
        checkPosition({point.x, point.y, z});
        const auto triangles = mesh.trianglesAt(point);
        if (triangles.empty())
            refuse("INVALID_REQUEST", "A lifted terrain vertex does not exist");
        const auto [it, inserted] = lifts.try_emplace(TerrainPositionKey::of(point), z);
        if (!inserted && it->second != z)
            refuse("INVALID_REQUEST", "A terrain vertex is lifted to two elevations");
        for (const auto &ref : triangles)
            touched.insert(leafOf(ref.tile));
    }
    std::vector<Leaf> addedLeaf;
    addedLeaf.reserve(change.added.size());
    for (const auto &triangle : change.added) {
        for (const auto &p : triangle)
            checkPosition(p);
        if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2])
            refuse("INVALID_REQUEST", "An added terrain triangle repeats a corner");
        const auto c = centroidOf(triangle);
        const auto cell = terrainCellOf(grid, c.x, c.y);
        if (!cell)
            refuse("INVALID_REQUEST", "Terrain edit lies outside the representable tile grid");
        const auto [row, column] = *cell;
        std::vector<std::string> paths;
        for (const auto *tile : mesh.cellTiles(row, column))
            paths.push_back(tile->content->key.path);
        std::string path;
        while (path.size() < terrainTileMaxDepth &&
               std::any_of(paths.begin(), paths.end(), [&](const std::string &p) {
                   return p.size() > path.size() && p.starts_with(path);
               }))
            path.push_back(char('0' + terrainQuadrantDigit(grid, c.x, c.y, path.size() + 1)));
        addedLeaf.push_back({row, column, path});
        touched.insert(addedLeaf.back());
    }
    TerrainRegionResult result;
    result.trianglesBefore = result.trianglesAfter = mesh.triangleCount();
    std::vector<std::pair<TerrainTileKey, std::vector<PlanTriangle>>> built;
    for (const auto &leaf : touched) {
        const auto &[row, column, path] = leaf;
        std::vector<PlanTriangle> triangles;
        for (const auto *tile : mesh.cellTiles(row, column)) {
            const auto &content = *tile->content;
            if (content.key.path != path)
                continue;
            result.replaced.push_back(content.key);
            result.trianglesAfter -= content.triangles.size();
            for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
                if (removed.contains({content.key, t}))
                    continue;
                PlanTriangle triangle;
                for (int corner = 0; corner < 3; ++corner) {
                    auto p = content.vertices[content.triangles[t][std::size_t(corner)]];
                    if (!lifts.empty())
                        if (const auto lift = lifts.find(TerrainPositionKey::of(p));
                            lift != lifts.end())
                            p.z = lift->second;
                    triangle[std::size_t(corner)] = p;
                }
                triangles.push_back(triangle);
            }
        }
        for (std::size_t i = 0; i < change.added.size(); ++i)
            if (addedLeaf[i] == leaf)
                triangles.push_back(change.added[i]);
        retile(grid, row, column, path, std::move(triangles), built);
    }
    std::sort(result.replaced.begin(), result.replaced.end());
    std::sort(built.begin(), built.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for (auto &[key, triangles] : built) {
        const auto *old = mesh.tile(key);
        // A tile keeps the name of the tile it replaces with the same key.
        if (old && !std::binary_search(result.replaced.begin(), result.replaced.end(), key))
            throw std::logic_error("A rebuilt terrain tile collides with an untouched tile");
        result.trianglesAfter += triangles.size();
        result.tiles.push_back(
            buildContent(key, old ? old->content->name : terrainTileKeyName(key), triangles));
    }
    return result;
}
void applyTerrainRegion(TerrainEditMesh &mesh, const TerrainRegionResult &result) {
    for (const auto &key : result.replaced)
        mesh.eraseTile(key);
    for (const auto &tile : result.tiles)
        mesh.insertTile(tile);
}

// ---- Commit ---------------------------------------------------------------------------------
namespace {
// The tiles' shared default material sides (R095.e2), or the "Terrain" material.
MaterialSides defaultTileMaterials(const Document &doc, const std::vector<BodyPtr> &tiles,
                                   std::optional<MaterialChange> &created) {
    std::optional<MaterialSides> shared;
    for (const auto &tile : tiles) {
        if (shared && *shared != tile->materials) {
            shared.reset();
            break;
        }
        shared = tile->materials;
    }
    if (shared)
        return *shared;
    for (const auto &[id, material] : doc.materials())
        if (material->name == terrainMaterialName)
            return {id, id};
    if (doc.materials().size() >= 1024)
        return {};
    const auto id = doc.nextMaterialId();
    created = MaterialChange{id, nullptr,
                             std::make_shared<MaterialRecord>(MaterialRecord{
                                 id, terrainMaterialName, terrainMaterialColor, 1, 0})};
    return {id, id};
}
} // namespace
TerrainTilesApplied applyTerrainTiles(Document &doc, Id terrain, TerrainRecord record,
                                      const TerrainRegionResult &result, std::string label) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || found->second->kind != BodyKind::Terrain ||
        !found->second->terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    std::vector<BodyPtr> current;
    std::map<TerrainTileKey, BodyPtr> byKey;
    std::size_t own = 0;
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &tile = doc.bodies().at(id);
        current.push_back(tile);
        byKey[tile->tileKey] = tile;
        own += tile->surface.faces.size();
    }
    const auto total = doc.materializedTotals().terrainTriangles;
    const auto others = total > own ? total - own : 0;
    std::size_t after = own;
    for (const auto &key : result.replaced) {
        const auto old = byKey.find(key);
        if (old == byKey.end())
            refuse("INVALID_REQUEST", "A replaced terrain tile does not exist");
        after -= old->second->surface.faces.size();
    }
    for (const auto &tile : result.tiles)
        after += tile->triangles.size();
    if (!after)
        refuse("INVALID_REQUEST", "A terrain needs at least one triangle");
    if (others + after > TerrainLimits::triangles)
        refuse("LIMIT_EXCEEDED", "Terrain exceeds the document terrain limit: terrain triangles "
                                 "would be " +
                                     groupedCount(others + after) + " of " +
                                     groupedCount(TerrainLimits::triangles));
    std::optional<MaterialChange> createdMaterial;
    std::optional<MaterialSides> fallback;
    TerrainTilesApplied applied;
    Edit edit{std::move(label), {}};
    // R096.e: the paint regions before and after (an edit appends entries, so the regions before
    // are the first ones after).
    const TerrainPaintRegions regionsBefore(*found->second->terrain), regionsAfter(record);
    std::set<TerrainTileKey> replaced(result.replaced.begin(), result.replaced.end()), kept;
    std::map<TerrainTileKey, BodyPtr> finalTiles = byKey;
    for (const auto &key : replaced)
        finalTiles.erase(key);
    Id next = doc.nextId();
    std::vector<std::shared_ptr<Body>> created;
    for (const auto &content : result.tiles) {
        if (finalTiles.contains(content->key))
            refuse("INVALID_REQUEST", "A rebuilt terrain tile collides with an untouched tile");
        const auto old = byKey.find(content->key);
        if (old != byKey.end() && replaced.contains(content->key)) {
            const auto mesh = terrainTileMesh(*old->second);
            if (mesh.vertices == content->vertices && mesh.triangles == content->triangles) {
                kept.insert(content->key);
                finalTiles[content->key] = old->second;
                continue;
            }
        }
        if (next == UINT64_MAX)
            refuse("LIMIT_EXCEEDED", "Entity identity space exhausted");
        auto body = std::make_shared<Body>(
            makeTerrainTile(next++, terrain, content->name, content->vertices, content->triangles));
        body->tileKey = content->key;
        // Tile-level paint follows the key, else the cell's first replaced tile, else the
        // terrain's shared default.
        const Body *source = old != byKey.end() ? old->second.get() : nullptr;
        if (!source)
            for (const auto &key : result.replaced)
                if (key.row == content->key.row && key.column == content->key.column) {
                    source = byKey.at(key).get();
                    break;
                }
        if (source) {
            body->materials = source->materials;
            body->color = source->color;
        } else {
            if (!fallback)
                fallback = defaultTileMaterials(doc, current, createdMaterial);
            body->materials = *fallback;
        }
        paintTerrainTile(*body, regionsAfter);
        finalTiles[content->key] = body;
        created.push_back(std::move(body));
    }
    for (const auto &key : replaced) {
        if (kept.contains(key))
            continue;
        const auto &old = byKey.at(key);
        applied.paintDropped.faces += terrainTileOwnPaint(*old, regionsBefore);
        applied.removed.push_back(old->id);
        edit.changes.push_back({old->id, old, nullptr, {}, {}, {}, true});
    }
    // R096.e: new paint regions repaint the faces of the tiles this edit keeps.
    if (regionsAfter.size() > regionsBefore.size()) {
        std::set<Id> made;
        for (const auto &body : created)
            made.insert(body->id);
        for (auto &[key, body] : finalTiles) {
            if (made.contains(body->id) || body->surface.vertices.empty())
                continue;
            std::array<double, 4> box{body->surface.vertices.begin()->second.x,
                                      body->surface.vertices.begin()->second.y,
                                      body->surface.vertices.begin()->second.x,
                                      body->surface.vertices.begin()->second.y};
            for (const auto &[id, p] : body->surface.vertices) {
                box[0] = std::min(box[0], p.x);
                box[1] = std::min(box[1], p.y);
                box[2] = std::max(box[2], p.x);
                box[3] = std::max(box[3], p.y);
            }
            if (!regionsAfter.meets(box, regionsBefore.size()))
                continue;
            auto painted = std::make_shared<Body>(*body);
            if (!paintTerrainTile(*painted, regionsAfter, regionsBefore.size()))
                continue;
            applied.repainted.push_back(body->id);
            edit.changes.push_back({body->id, body, painted, {}, {}, {}, true});
            body = std::move(painted);
        }
    }
    // R096.f1b: the digest of the new tiles (key order) is deferred: computed once, from these
    // immutable tile records, by its first reader or a worker (TerrainDigest), not here on the
    // commit's critical path. Its definition is unchanged.
    {
        std::vector<std::shared_ptr<const void>> tiles;
        tiles.reserve(finalTiles.size());
        for (const auto &[key, body] : finalTiles)
            tiles.push_back(body);
        record.meshDigest = TerrainDigest::deferred(
            [](const std::vector<const void *> &held) {
                std::vector<const Body *> ordered;
                ordered.reserve(held.size());
                for (const auto *tile : held)
                    ordered.push_back(static_cast<const Body *>(tile));
                return terrainMeshDigest(ordered);
            },
            std::move(tiles));
    }
    validateTerrainRecord(record);
    if (createdMaterial)
        edit.materials.push_back(std::move(*createdMaterial));
    const auto &before = found->second;
    auto container = std::make_shared<Body>(*before);
    container->terrain = std::move(record);
    edit.changes.insert(edit.changes.begin(), {terrain, before, std::move(container), {}, {}, {}, true});
    for (auto &body : created) {
        applied.added.push_back(body->id);
        edit.changes.push_back({body->id, nullptr, std::move(body), {}, {}, {}, true});
    }
    applied.changes = doc.apply(std::move(edit), doc.revision());
    return applied;
}

// ---- Seams ----------------------------------------------------------------------------------
namespace {
struct PlanKey {
    std::uint64_t x{}, y{};
    bool operator==(const PlanKey &) const = default;
};
struct PlanHash {
    std::size_t operator()(const PlanKey &key) const {
        std::uint64_t h = key.x * 0x9E3779B97F4A7C15ULL;
        h ^= key.y + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        return std::size_t(h);
    }
};
std::uint64_t bitsOf(double v) { return std::bit_cast<std::uint64_t>(v == 0 ? 0.0 : v); }
} // namespace
TerrainSeamReport checkTerrainSeams(const std::vector<TerrainTilePtr> &tiles) {
    TerrainSeamReport report;
    auto problem = [&](std::string text) {
        if (report.problems.size() < 32)
            report.problems.push_back(std::move(text));
    };
    auto where = [](Vec3 p) {
        return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
    };
    std::unordered_map<PlanKey, std::uint32_t, PlanHash> index;
    std::vector<Vec3> points;
    std::unordered_map<std::uint64_t, std::uint32_t> directed;
    for (const auto &tile : tiles) {
        std::vector<std::uint32_t> global(tile->vertices.size());
        for (std::size_t i = 0; i < tile->vertices.size(); ++i) {
            const auto p = tile->vertices[i];
            const auto [it, inserted] =
                index.try_emplace(PlanKey{bitsOf(p.x), bitsOf(p.y)}, std::uint32_t(points.size()));
            if (inserted)
                points.push_back(p);
            else if (bitsOf(points[it->second].z) != bitsOf(p.z)) {
                ++report.zMismatches;
                problem("copies of the vertex at " + where(p) + " differ in Z (" +
                        std::to_string(points[it->second].z) + " and " + std::to_string(p.z) +
                        ")");
            }
            global[i] = it->second;
        }
        for (const auto &t : tile->triangles)
            for (int i = 0; i < 3; ++i) {
                const auto a = global[t[std::size_t(i)]], b = global[t[std::size_t((i + 1) % 3)]];
                if (++directed[(std::uint64_t(a) << 32) | b] == 2) {
                    ++report.nonManifoldEdges;
                    problem("the edge from " + where(points[a]) + " to " + where(points[b]) +
                            " is used twice in one direction");
                }
            }
    }
    report.positions = points.size();
    std::vector<std::pair<std::uint32_t, std::uint32_t>> boundary;
    for (const auto &[edge, count] : directed) {
        const auto a = std::uint32_t(edge >> 32), b = std::uint32_t(edge & 0xffffffffu);
        if (directed.contains((std::uint64_t(b) << 32) | a)) {
            if (a < b)
                ++report.edges;
        } else {
            ++report.edges;
            boundary.push_back({a, b});
        }
    }
    std::sort(boundary.begin(), boundary.end());
    report.boundaryEdges = boundary.size();
    // Cracks: a boundary vertex strictly inside another boundary edge in plan.
    std::vector<std::uint32_t> boundaryVertices;
    double lengthSum = 0;
    for (const auto &[a, b] : boundary) {
        boundaryVertices.push_back(a);
        boundaryVertices.push_back(b);
        lengthSum += std::hypot(points[b].x - points[a].x, points[b].y - points[a].y);
    }
    std::sort(boundaryVertices.begin(), boundaryVertices.end());
    boundaryVertices.erase(std::unique(boundaryVertices.begin(), boundaryVertices.end()),
                           boundaryVertices.end());
    if (boundary.empty())
        return report;
    const double cell = std::max(lengthSum / double(boundary.size()), 1e-6);
    auto cellOf = [&](double v) { return std::int64_t(std::floor(v / cell)); };
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::uint32_t>> grid;
    for (const auto v : boundaryVertices)
        grid[{cellOf(points[v].x), cellOf(points[v].y)}].push_back(v);
    constexpr double near = 1e-9;
    for (const auto &[a, b] : boundary) {
        const auto pa = points[a], pb = points[b];
        const double length = std::hypot(pb.x - pa.x, pb.y - pa.y);
        if (!(length > 0))
            continue;
        const auto x0 = cellOf(std::min(pa.x, pb.x) - near), x1 = cellOf(std::max(pa.x, pb.x) + near),
                   y0 = cellOf(std::min(pa.y, pb.y) - near), y1 = cellOf(std::max(pa.y, pb.y) + near);
        // The cells to search: the edge's box when it is small; for a long edge the cells along
        // it (each step at most one cell, with its eight neighbours), or every boundary vertex
        // when even that walk would be longer than the list.
        std::vector<const std::vector<std::uint32_t> *> lists;
        if ((x1 - x0 + 1) * (y1 - y0 + 1) <= 4096) {
            for (auto x = x0; x <= x1; ++x)
                for (auto y = y0; y <= y1; ++y)
                    if (const auto found = grid.find({x, y}); found != grid.end())
                        lists.push_back(&found->second);
        } else if (length / cell < double(boundaryVertices.size())) {
            const auto steps = std::size_t(std::ceil(length / cell)) + 1;
            std::set<std::pair<std::int64_t, std::int64_t>> cells;
            for (std::size_t step = 0; step <= steps; ++step) {
                const double t = double(step) / double(steps);
                const auto cx = cellOf(pa.x + (pb.x - pa.x) * t),
                           cy = cellOf(pa.y + (pb.y - pa.y) * t);
                for (auto dx = -1; dx <= 1; ++dx)
                    for (auto dy = -1; dy <= 1; ++dy)
                        cells.insert({cx + dx, cy + dy});
            }
            for (const auto &c : cells)
                if (const auto found = grid.find(c); found != grid.end())
                    lists.push_back(&found->second);
        } else
            lists.push_back(&boundaryVertices);
        {
            for (const auto *list : lists)
                for (const auto v : *list) {
                    if (v == a || v == b)
                        continue;
                    const auto p = points[v];
                    const double t = ((p.x - pa.x) * (pb.x - pa.x) + (p.y - pa.y) * (pb.y - pa.y)) /
                                     (length * length);
                    const double distance =
                        std::abs((pb.x - pa.x) * (p.y - pa.y) - (pb.y - pa.y) * (p.x - pa.x)) /
                        length;
                    if (distance <= near && t * length > near && (1 - t) * length > near) {
                        ++report.cracks;
                        problem("the vertex at " + where(p) +
                                " lies inside the boundary edge from " + where(pa) + " to " +
                                where(pb) + " (a crack between tiles)");
                    }
                }
            }
    }
    return report;
}
TerrainSeamReport checkTerrainSeams(const Document &doc, Id terrain) {
    if (!isTerrain(doc, terrain))
        refuse("NOT_FOUND", "Terrain body does not exist");
    std::vector<TerrainTilePtr> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(terrainTileContent(*doc.bodies().at(id)));
    return checkTerrainSeams(tiles);
}

// ---- Edit kinds -----------------------------------------------------------------------------
namespace {
std::vector<TerrainEditKind> &kinds() {
    // R096.c: the source edits are built in; R096.b: sculpt and smooth; R096.d: stamp; R096.e:
    // drape.
    static std::vector<TerrainEditKind> registered = [] {
        auto list = terrainSourceEditKinds();
        for (auto &kind : terrainSculptKinds())
            list.push_back(std::move(kind));
        for (auto &kind : terrainStampKinds())
            list.push_back(std::move(kind));
        for (auto &kind : terrainDrapeKinds())
            list.push_back(std::move(kind));
        return list;
    }();
    return registered;
}
} // namespace
void registerTerrainEditKind(TerrainEditKind kind) {
    if (kind.name.empty() || kind.name.size() > TerrainLimits::editKindBytes ||
        (!kind.apply && !kind.applyInContext))
        throw std::invalid_argument("Invalid terrain edit kind");
    for (auto &existing : kinds())
        if (existing.name == kind.name) {
            existing = std::move(kind);
            return;
        }
    kinds().push_back(std::move(kind));
}
const TerrainEditKind *findTerrainEditKind(const std::string &name) {
    for (const auto &kind : kinds())
        if (kind.name == name)
            return &kind;
    return nullptr;
}
namespace {
bool readsSources(const TerrainEditKind *kind) {
    return kind && (kind->applyInContext || kind->sources || kind->protects ||
                    kind->protectedPoints);
}
// R096.d: the entry with the data its kind recomputed (or cleared), or the entry itself when the
// data stands, so a list that did not change keeps its pointers.
TerrainEditPtr withData(const TerrainEditPtr &edit, const std::shared_ptr<const TerrainEditData> &data) {
    if (!data || data == edit->data || (edit->data && data->equals(*edit->data)))
        return edit;
    return std::make_shared<const TerrainEdit>(TerrainEdit{edit->id, edit->kind, data});
}
TerrainEditOutcome applyKind(const TerrainEditKind &kind, const TerrainEditMesh &mesh,
                             const TerrainSourceState *state, const TerrainParameters &parameters,
                             const TerrainEdit &edit, TerrainProgress *progress = nullptr) {
    if (kind.applyInContext)
        return kind.applyInContext({mesh, *state, parameters, progress}, edit);
    return kind.apply(mesh, edit);
}
} // namespace
TerrainReplayResult replayTerrainEdits(const TerrainTileSet &base,
                                       const std::vector<TerrainEditPtr> &edits,
                                       TerrainProgress *progress, const TerrainSources &sources,
                                       const TerrainParameters &parameters) {
    terrainCheckpoint(progress, TerrainStage::Replay, 0);
    TerrainReplayResult result;
    result.tiles.grid = base.grid;
    if (edits.empty()) {
        result.tiles = base;
        terrainCheckpoint(progress, TerrainStage::Replay, 1);
        return result;
    }
    result.edits.reserve(edits.size());
    TerrainEditMesh mesh(base.grid);
    for (const auto &tile : base.tiles)
        mesh.insertTile(tile);
    // R096.c: the current sources, derived as the record's are (every entry's effect in order,
    // applied or skipped), only when some entry reads or changes them.
    std::optional<TerrainSourceState> state;
    for (const auto &edit : edits)
        if (readsSources(findTerrainEditKind(edit->kind))) {
            state.emplace(sources);
            break;
        }
    for (std::size_t i = 0; i < edits.size(); ++i) {
        terrainCheckpoint(progress, TerrainStage::Replay, double(i) / double(edits.size()));
        const auto &edit = *edits[i];
        const auto *kind = findTerrainEditKind(edit.kind);
        // R096.d: the entry as replayed (its kind may recompute or clear its resolved geometry).
        auto entry = edits[i];
        auto skip = [&](std::string notice) {
            result.skipped.push_back({edit.id, edit.kind, std::move(notice)});
            if (kind && kind->unresolved)
                entry = withData(edits[i], kind->unresolved(edit));
        };
        if (!kind) {
            skip("Unknown terrain edit kind");
            result.edits.push_back(entry);
            continue;
        }
        // One entry; its effect on the current sources is applied after it, applied or skipped.
        const auto replayEntry = [&] {
            // A refusal of the change is the entry no longer applying, like an empty outcome.
            try {
                const auto outcome =
                    applyKind(*kind, mesh, state ? &*state : nullptr, parameters, edit, progress);
                if (!outcome.change) {
                    skip(outcome.notice.empty() ? "The change no longer applies" : outcome.notice);
                    return;
                }
                const auto region = replaceTerrainRegion(mesh, *outcome.change);
                if (!region.trianglesAfter) {
                    skip("The change would leave the terrain without triangles");
                    return;
                }
                applyTerrainRegion(mesh, region);
                entry = withData(edits[i], outcome.data);
                ++result.applied;
            } catch (const TerrainError &error) {
                skip(error.what());
                result.skipped.back().error = std::current_exception();
            }
        };
        replayEntry();
        if (state)
            applyTerrainSourceEffect(*state, *kind, *entry);
        result.edits.push_back(entry);
    }
    for (const auto *tile : mesh.tiles())
        result.tiles.tiles.push_back(tile->content);
    terrainCheckpoint(progress, TerrainStage::Replay, 1);
    return result;
}
void requireLevelTerrain(const Document &doc, Id terrain) {
    const auto world = doc.worldTransform(terrain);
    const auto &m = world.m;
    constexpr double eps = 1e-12;
    if (std::abs(m[2]) > eps || std::abs(m[6]) > eps || std::abs(m[8]) > eps ||
        std::abs(m[9]) > eps || !(m[10] > 0))
        refuse("INVALID_OPERATION",
               "Terrain edits need a level terrain; this terrain is rotated off the vertical");
}
namespace {
struct Computed {
    std::uint32_t id{};
    TerrainRecord record;
    TerrainRegionResult region;
    std::size_t removed{}, added{}, lifted{};
};
// Everything applyTerrainEdit does before the commit; previewTerrainEdit stops here.
Computed computeTerrainEdit(const Document &doc, Id terrain, const std::string &kindName,
                            std::shared_ptr<const TerrainEditData> data,
                            const TerrainEditOutcome *planned = nullptr) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || found->second->kind != BodyKind::Terrain ||
        !found->second->terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    requireLevelTerrain(doc, terrain);
    const auto *kind = findTerrainEditKind(kindName);
    if (!kind)
        refuse("INVALID_REQUEST", "Unknown terrain edit kind");
    if (!data)
        refuse("INVALID_REQUEST", "A terrain edit needs its parameters");
    auto record = *found->second->terrain;
    if (record.edits.size() >= TerrainLimits::edits)
        refuse("LIMIT_EXCEEDED",
               "The terrain's edit list is full (4,096 changes); use Reset edits, or Explode "
               "terrain to keep the surface as ordinary geometry");
    // IDs come from the record's counter and are never reused; the last one is refused rather
    // than wrapped.
    const auto id = record.nextEditId;
    if (id == std::numeric_limits<std::uint32_t>::max())
        refuse("LIMIT_EXCEEDED", "The terrain has used every change ID; use Reset edits, or "
                                 "Explode terrain to keep the surface as ordinary geometry");
    auto entry = std::make_shared<TerrainEdit>(TerrainEdit{id, kindName, std::move(data)});
    // The bound counts what validateTerrainRecord counts, so a refusal is LIMIT_EXCEEDED here.
    if (terrainEditBytes(record.edits) + terrainEditEntryBytes(*entry) > TerrainLimits::editBytes)
        refuse("LIMIT_EXCEEDED",
               "The terrain's edit list would exceed 8 MiB; use Reset edits, or Explode terrain "
               "to keep the surface as ordinary geometry");
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    std::shared_ptr<const TerrainSourceState> state;
    if (readsSources(kind) && !planned)
        state = terrainSourceState(record);
    const auto outcome =
        planned ? *planned : applyKind(*kind, *mesh, state.get(), record.parameters, *entry);
    if (!outcome.change)
        refuse("INVALID_REQUEST", outcome.notice.empty() ? "The terrain edit does not apply here"
                                                          : outcome.notice);
    // R096.d: the entry is stored with the resolved geometry its kind computed, which counts
    // against the list's bound too.
    if (outcome.data && !outcome.data->equals(*entry->data)) {
        entry = std::make_shared<TerrainEdit>(TerrainEdit{id, kindName, outcome.data});
        if (terrainEditBytes(record.edits) + terrainEditEntryBytes(*entry) > TerrainLimits::editBytes)
            refuse("LIMIT_EXCEEDED",
                   "The terrain's edit list would exceed 8 MiB; use Reset edits, or Explode "
                   "terrain to keep the surface as ordinary geometry");
    }
    Computed computed;
    computed.region = replaceTerrainRegion(*mesh, *outcome.change);
    computed.lifted = outcome.change->lifted.size();
    computed.removed = outcome.change->removed.size();
    computed.added = outcome.change->added.size();
    computed.id = id;
    record.edits.push_back(std::move(entry));
    record.nextEditId = id + 1;
    record.generator = terrainEditGenerator;
    computed.record = std::move(record);
    return computed;
}
} // namespace
TerrainEditApplied applyTerrainEdit(Document &doc, Id terrain, const std::string &kindName,
                                    std::shared_ptr<const TerrainEditData> data,
                                    std::string label, const TerrainEditOutcome *planned) {
    auto computed = computeTerrainEdit(doc, terrain, kindName, std::move(data), planned);
    TerrainEditApplied applied;
    applied.edit = computed.id;
    applied.tiles = applyTerrainTiles(doc, terrain, std::move(computed.record), computed.region,
                                      std::move(label));
    return applied;
}
TerrainEditPreview previewTerrainEdit(const Document &doc, Id terrain, const std::string &kindName,
                                      std::shared_ptr<const TerrainEditData> data,
                                      const TerrainEditOutcome *planned) {
    auto computed = computeTerrainEdit(doc, terrain, kindName, std::move(data), planned);
    TerrainEditPreview preview;
    preview.edit = computed.id;
    preview.region = std::move(computed.region);
    preview.removedTriangles = computed.removed;
    preview.addedTriangles = computed.added;
    preview.liftedVertices = computed.lifted;
    return preview;
}
} // namespace sketchy
