#pragma once
// R096.c's view of an edit mesh by exact position (moved here for R096.d's stamp): the triangles
// at a position, edges and their sides, the triangles holding a point and those a segment
// touches, all in TerrainTriangleRef order. Closed plan geometry with the adapter's exact
// orientation.
#include "core/terrain_edit_mesh.hpp"
#include "geometry/terrain_triangulation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace sketchy {
namespace terrain_view {
inline TerrainVec2 v2(const Vec3 &p) { return {p.x, p.y}; }
inline bool inTriangle(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c, TerrainVec2 p) {
    return terrainOrientation(a, b, p) >= 0 && terrainOrientation(b, c, p) >= 0 &&
           terrainOrientation(c, a, p) >= 0;
}
inline bool segmentsMeet(TerrainVec2 p, TerrainVec2 q, TerrainVec2 a, TerrainVec2 b) {
    const int o1 = terrainOrientation(p, q, a), o2 = terrainOrientation(p, q, b),
              o3 = terrainOrientation(a, b, p), o4 = terrainOrientation(a, b, q);
    if (o1 * o2 > 0 || o3 * o4 > 0)
        return false;
    if (o1 == 0 && o2 == 0) // collinear: the projections must overlap
        return std::max(std::min(p.x, q.x), std::min(a.x, b.x)) <=
                   std::min(std::max(p.x, q.x), std::max(a.x, b.x)) &&
               std::max(std::min(p.y, q.y), std::min(a.y, b.y)) <=
                   std::min(std::max(p.y, q.y), std::max(a.y, b.y));
    return true;
}
} // namespace terrain_view
class TerrainMeshView {
  public:
    explicit TerrainMeshView(const TerrainEditMesh &mesh) : mesh_(mesh) {}
    const TerrainEditMesh &mesh() const { return mesh_; }
    std::array<Vec3, 3> corners(const TerrainTriangleRef &ref) const {
        const auto &content = *tile(ref.tile).content;
        const auto &t = content.triangles[ref.triangle];
        return {content.vertices[t[0]], content.vertices[t[1]], content.vertices[t[2]]};
    }
    const std::vector<TerrainTriangleRef> &at(const Vec3 &p) const {
        const auto key = TerrainPositionKey::of(p);
        auto found = incident_.find(key);
        if (found == incident_.end())
            found = incident_.emplace(key, mesh_.trianglesAt(p)).first;
        return found->second;
    }
    // The triangles with both corners: one or two (an edge), none when it is not an edge.
    std::vector<TerrainTriangleRef> edge(const Vec3 &a, const Vec3 &b) const {
        const auto &l = at(a), &r = at(b);
        std::vector<TerrainTriangleRef> shared;
        std::set_intersection(l.begin(), l.end(), r.begin(), r.end(), std::back_inserter(shared));
        return shared;
    }
    bool boundaryEdge(const Vec3 &a, const Vec3 &b) const { return edge(a, b).size() == 1; }
    bool boundaryVertex(const Vec3 &p) const {
        for (const auto &ref : at(p)) {
            const auto c = corners(ref);
            for (int i = 0; i < 3; ++i)
                if (c[std::size_t(i)] == p && (boundaryEdge(p, c[std::size_t((i + 1) % 3)]) ||
                                               boundaryEdge(p, c[std::size_t((i + 2) % 3)])))
                    return true;
        }
        return false;
    }
    std::optional<TerrainTriangleRef> across(const TerrainTriangleRef &from, const Vec3 &a,
                                             const Vec3 &b) const {
        for (const auto &ref : edge(a, b))
            if (!(ref == from))
                return ref;
        return std::nullopt;
    }
    // Triangles whose closed region contains (x, y), in reference order.
    std::vector<TerrainTriangleRef> containing(TerrainVec2 p) const {
        std::vector<TerrainTriangleRef> result;
        for (const auto *tile : mesh_.tilesAt(p.x, p.y, 0)) {
            const auto &content = *tile->content;
            for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
                const auto &tri = content.triangles[t];
                const auto a = terrain_view::v2(content.vertices[tri[0]]), b = terrain_view::v2(content.vertices[tri[1]]),
                           c = terrain_view::v2(content.vertices[tri[2]]);
                if (p.x < std::min({a.x, b.x, c.x}) || p.x > std::max({a.x, b.x, c.x}) ||
                    p.y < std::min({a.y, b.y, c.y}) || p.y > std::max({a.y, b.y, c.y}))
                    continue;
                if (terrain_view::inTriangle(a, b, c, p))
                    result.push_back({content.key, t});
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }
    // Triangles whose closed region meets the closed segment, in reference order. The tiles come
    // from the grid index along the segment (a box of at most one cell per step), and a triangle
    // wholly more than 1e-6 m to one side of the segment's line is skipped before the exact test.
    std::vector<TerrainTriangleRef> touching(TerrainVec2 p, TerrainVec2 q) const {
        const double cell = mesh_.grid().cell > 0 ? mesh_.grid().cell : 1.0;
        const double dx = q.x - p.x, dy = q.y - p.y;
        const double length = std::hypot(dx, dy);
        const auto steps = std::size_t(std::clamp(std::ceil(length / cell), 1.0, 1e6));
        std::vector<const TerrainEditMesh::Tile *> tiles;
        for (std::size_t i = 0; i < steps; ++i) {
            const double t0 = double(i) / double(steps), t1 = double(i + 1) / double(steps);
            const double x0 = p.x + dx * t0, y0 = p.y + dy * t0;
            const double x1 = i + 1 == steps ? q.x : p.x + dx * t1,
                         y1 = i + 1 == steps ? q.y : p.y + dy * t1;
            for (const auto *tile : mesh_.tilesIn(std::min(x0, x1), std::min(y0, y1),
                                                  std::max(x0, x1), std::max(y0, y1), 1e-6))
                tiles.push_back(tile);
        }
        std::sort(tiles.begin(), tiles.end(), [](const auto *a, const auto *b) {
            return a->content->key < b->content->key;
        });
        tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
        const double minX = std::min(p.x, q.x), maxX = std::max(p.x, q.x),
                     minY = std::min(p.y, q.y), maxY = std::max(p.y, q.y);
        const double margin = 1e-6 * (std::abs(dx) + std::abs(dy));
        std::vector<TerrainTriangleRef> result;
        for (const auto *tile : tiles) {
            const auto &content = *tile->content;
            for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
                const auto &tri = content.triangles[t];
                const auto a = terrain_view::v2(content.vertices[tri[0]]), b = terrain_view::v2(content.vertices[tri[1]]),
                           c = terrain_view::v2(content.vertices[tri[2]]);
                if (maxX < std::min({a.x, b.x, c.x}) || minX > std::max({a.x, b.x, c.x}) ||
                    maxY < std::min({a.y, b.y, c.y}) || minY > std::max({a.y, b.y, c.y}))
                    continue;
                const auto side = [&](TerrainVec2 v) { return dx * (v.y - p.y) - dy * (v.x - p.x); };
                const double sa = side(a), sb = side(b), sc = side(c);
                if ((sa > margin && sb > margin && sc > margin) ||
                    (sa < -margin && sb < -margin && sc < -margin))
                    continue;
                if (terrain_view::inTriangle(a, b, c, p) || terrain_view::inTriangle(a, b, c, q) || terrain_view::segmentsMeet(p, q, a, b) ||
                    terrain_view::segmentsMeet(p, q, b, c) || terrain_view::segmentsMeet(p, q, c, a))
                    result.push_back({content.key, t});
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

  private:
    const TerrainEditMesh::Tile &tile(const TerrainTileKey &key) const {
        if (last_ && last_->content->key == key)
            return *last_;
        const auto *found = mesh_.tile(key);
        if (!found)
            throw std::logic_error("A terrain triangle names a missing tile");
        last_ = found;
        return *found;
    }
    const TerrainEditMesh &mesh_;
    mutable const TerrainEditMesh::Tile *last_{};
    mutable std::unordered_map<TerrainPositionKey, std::vector<TerrainTriangleRef>,
                               TerrainPositionHash>
        incident_;
};

} // namespace sketchy
