#pragma once
// R096.d plan polygon index (ADR 0169 §4.3, decision 6): a closed plan polygon (nonzero winding,
// the adapter's exact orientation) indexed for many queries, so that the pad stamp, the pad
// protection and the clipping of current sources do not scan every edge per query.
//
// - Containment reads one horizontal band of edges (min(4,096, corners) bands of equal height,
//   each edge listed in the bands its Y range meets), so its work is the busiest band's edge
//   count (`busiestBand`), which callers bound.
// - Distances and segment queries descend a bounding-volume tree of the edges.
// Every answer is a pure function of the polygon and the query: the bands and the tree only
// order the work.
#include "geometry/terrain_triangulation.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
namespace sketchy {
// A static bounding-volume tree over boxes (minX, minY, maxX, maxY), split at the median centre
// along the longer side. `visit` calls back every box that overlaps the query box (closed), in
// no particular order; the callback returns false to stop.
class TerrainBoxTree {
  public:
    using Box = std::array<double, 4>;
    TerrainBoxTree() = default;
    explicit TerrainBoxTree(std::vector<Box> boxes);
    bool empty() const { return boxes_.empty(); }
    const Box &box(std::uint32_t i) const { return boxes_[i]; }
    std::size_t size() const { return boxes_.size(); }
    // Visits the boxes meeting `query`; `prune` (optional) may refuse a node's box (for a
    // segment, the boxes its line misses).
    void visit(const Box &query, const std::function<bool(std::uint32_t)> &callback,
               const std::function<bool(const Box &)> &prune = {}) const;

  private:
    struct Node {
        Box box;
        std::uint32_t first, count, left, right; // count 0: inner node
    };
    std::uint32_t build(std::uint32_t first, std::uint32_t count);
    std::vector<Box> boxes_;
    std::vector<std::uint32_t> order_;
    std::vector<Node> nodes_;
};

class TerrainPolygonIndex {
  public:
    using Point = std::array<double, 2>;
    TerrainPolygonIndex() = default;
    // The polygon's corners (closing edge implied); fewer than three give an empty region.
    explicit TerrainPolygonIndex(std::vector<Point> polygon);
    const std::vector<Point> &polygon() const { return polygon_; }
    // minX, minY, maxX, maxY of the corners.
    const std::array<double, 4> &box() const { return box_; }
    bool empty() const { return polygon_.size() < 3; }
    // Edges in the busiest band: the most edges one containment test reads.
    std::size_t busiestBand() const { return busiest_; }
    double busiestBandY() const { return busiestY_; }
    // Closed containment, exact (on an edge counts as inside).
    bool contains(double x, double y) const;
    // Whether the closed polygon widened by `slack` metres (>= 0) holds the point: contained, or
    // within `slack` (plan distance, binary64) of an edge.
    bool near(double x, double y, double slack) const;
    // Whether the closed polygon widened by `slack` meets the closed segment: an end near it, an
    // edge crossing it (exact), or an edge within `slack` of it.
    bool segmentNear(TerrainVec2 a, TerrainVec2 b, double slack) const;

  private:
    std::vector<Point> polygon_;
    std::array<double, 4> box_{};
    double bandLow_{}, bandHeight_{1};
    std::vector<std::vector<std::uint32_t>> bands_;
    std::size_t busiest_{};
    double busiestY_{};
    TerrainBoxTree edges_;
};
// The plan distance between two closed segments (zero when they meet, exact test), binary64.
double terrainSegmentDistance(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c, TerrainVec2 d);
// Whether two closed segments meet (exact orientation).
bool terrainSegmentsMeet(TerrainVec2 p, TerrainVec2 q, TerrainVec2 a, TerrainVec2 b);
} // namespace sketchy
