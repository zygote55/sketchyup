#include "core/terrain_plan_index.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace sketchy {
namespace {
using Point = TerrainPolygonIndex::Point;
int orient(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c) { return terrainOrientation(a, b, c); }
TerrainVec2 v2(const Point &p) { return {p[0], p[1]}; }
// Lengths by the correctly rounded square root (not hypot), so no C library's last bit enters
// replayed results.
double planLength(double dx, double dy) { return std::sqrt(dx * dx + dy * dy); }
double pointSegment(TerrainVec2 p, TerrainVec2 a, TerrainVec2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
    if (!(length > 0))
        return planLength(p.x - a.x, p.y - a.y);
    const double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / length;
    if (t <= 0)
        return planLength(p.x - a.x, p.y - a.y);
    if (t >= 1)
        return planLength(p.x - b.x, p.y - b.y);
    return planLength(a.x + t * dx - p.x, a.y + t * dy - p.y);
}
} // namespace

bool terrainSegmentsMeet(TerrainVec2 p, TerrainVec2 q, TerrainVec2 a, TerrainVec2 b) {
    const int o1 = orient(p, q, a), o2 = orient(p, q, b), o3 = orient(a, b, p), o4 = orient(a, b, q);
    if (o1 * o2 > 0 || o3 * o4 > 0)
        return false;
    if (o1 == 0 && o2 == 0) // collinear: the projections must overlap
        return std::max(std::min(p.x, q.x), std::min(a.x, b.x)) <=
                   std::min(std::max(p.x, q.x), std::max(a.x, b.x)) &&
               std::max(std::min(p.y, q.y), std::min(a.y, b.y)) <=
                   std::min(std::max(p.y, q.y), std::max(a.y, b.y));
    return true;
}
double terrainSegmentDistance(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c, TerrainVec2 d) {
    if (terrainSegmentsMeet(a, b, c, d))
        return 0;
    return std::min({pointSegment(a, c, d), pointSegment(b, c, d), pointSegment(c, a, b),
                     pointSegment(d, a, b)});
}

// ---- Box tree -------------------------------------------------------------------------------
TerrainBoxTree::TerrainBoxTree(std::vector<Box> boxes) : boxes_(std::move(boxes)) {
    if (boxes_.empty())
        return;
    order_.resize(boxes_.size());
    for (std::uint32_t i = 0; i < order_.size(); ++i)
        order_[i] = i;
    nodes_.reserve(boxes_.size() / 2 + 2);
    build(0, std::uint32_t(boxes_.size()));
}
std::uint32_t TerrainBoxTree::build(std::uint32_t first, std::uint32_t count) {
    const auto index = std::uint32_t(nodes_.size());
    nodes_.push_back({});
    Node node{{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
               -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()},
              first, count, 0, 0};
    for (std::uint32_t i = first; i < first + count; ++i) {
        const auto &b = boxes_[order_[i]];
        node.box[0] = std::min(node.box[0], b[0]);
        node.box[1] = std::min(node.box[1], b[1]);
        node.box[2] = std::max(node.box[2], b[2]);
        node.box[3] = std::max(node.box[3], b[3]);
    }
    if (count > 4) {
        const bool alongX = node.box[2] - node.box[0] >= node.box[3] - node.box[1];
        auto centre = [&](std::uint32_t i) {
            const auto &b = boxes_[i];
            return alongX ? b[0] + b[2] : b[1] + b[3];
        };
        const auto half = count / 2;
        std::nth_element(order_.begin() + first, order_.begin() + first + half,
                         order_.begin() + first + count, [&](std::uint32_t a, std::uint32_t b) {
                             const double ca = centre(a), cb = centre(b);
                             return ca < cb || (ca == cb && a < b);
                         });
        node.count = 0;
        node.left = build(first, half);
        node.right = build(first + half, count - half);
    }
    nodes_[index] = node;
    return index;
}
void TerrainBoxTree::visit(const Box &query, const std::function<bool(std::uint32_t)> &callback,
                           const std::function<bool(const Box &)> &prune) const {
    if (nodes_.empty())
        return;
    auto meets = [&](const Box &b) {
        return !(b[2] < query[0] || b[0] > query[2] || b[3] < query[1] || b[1] > query[3]);
    };
    // The median split halves every level, so 64 levels hold any vector the memory can.
    std::uint32_t stack[128];
    std::size_t depth = 0;
    stack[depth++] = 0;
    while (depth) {
        const auto &node = nodes_[stack[--depth]];
        if (!meets(node.box) || (prune && !prune(node.box)))
            continue;
        if (node.count) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i)
                if (meets(boxes_[order_[i]]) && !callback(order_[i]))
                    return;
            continue;
        }
        stack[depth++] = node.right;
        stack[depth++] = node.left;
    }
}

// ---- Polygon --------------------------------------------------------------------------------
TerrainPolygonIndex::TerrainPolygonIndex(std::vector<Point> polygon) : polygon_(std::move(polygon)) {
    const std::size_t n = polygon_.size();
    if (n < 3)
        return;
    box_ = {polygon_[0][0], polygon_[0][1], polygon_[0][0], polygon_[0][1]};
    for (const auto &p : polygon_) {
        box_[0] = std::min(box_[0], p[0]);
        box_[1] = std::min(box_[1], p[1]);
        box_[2] = std::max(box_[2], p[0]);
        box_[3] = std::max(box_[3], p[1]);
    }
    const std::size_t count = std::min<std::size_t>(4096, n);
    bandLow_ = box_[1];
    bandHeight_ = (box_[3] - box_[1]) / double(count);
    std::size_t bands = count;
    if (!(bandHeight_ > 0) || !std::isfinite(bandHeight_)) {
        bands = 1;
        bandHeight_ = std::numeric_limits<double>::infinity();
    }
    auto bandOf = [&](double y) {
        const double t = std::floor((y - bandLow_) / bandHeight_);
        return std::size_t(std::clamp(std::isfinite(t) ? t : 0.0, 0.0, double(bands - 1)));
    };
    bands_.assign(bands, {});
    std::vector<TerrainBoxTree::Box> boxes;
    boxes.reserve(n);
    for (std::uint32_t e = 0; e < n; ++e) {
        const auto &a = polygon_[e], &b = polygon_[(e + 1) % n];
        const double lo = std::min(a[1], b[1]), hi = std::max(a[1], b[1]);
        for (auto band = bandOf(lo); band <= bandOf(hi); ++band)
            bands_[band].push_back(e);
        boxes.push_back({std::min(a[0], b[0]), lo, std::max(a[0], b[0]), hi});
    }
    for (std::size_t band = 0; band < bands; ++band)
        if (bands_[band].size() > busiest_) {
            busiest_ = bands_[band].size();
            busiestY_ = std::isfinite(bandHeight_) ? bandLow_ + (double(band) + 0.5) * bandHeight_
                                                   : bandLow_;
        }
    edges_ = TerrainBoxTree(std::move(boxes));
}
bool TerrainPolygonIndex::contains(double x, double y) const {
    if (empty() || x < box_[0] || x > box_[2] || y < box_[1] || y > box_[3])
        return false;
    const std::size_t n = polygon_.size();
    const double t = std::floor((y - bandLow_) / bandHeight_);
    const auto band =
        std::size_t(std::clamp(std::isfinite(t) ? t : 0.0, 0.0, double(bands_.size() - 1)));
    int winding = 0;
    const TerrainVec2 q{x, y};
    for (const auto e : bands_[band]) {
        const auto a = v2(polygon_[e]), b = v2(polygon_[(e + 1) % n]);
        if (y < std::min(a.y, b.y) || y > std::max(a.y, b.y))
            continue;
        const int o = orient(a, b, q);
        if (o == 0 && std::min(a.x, b.x) <= x && x <= std::max(a.x, b.x))
            return true;
        if (a.y <= y) {
            if (b.y > y && o > 0)
                ++winding;
        } else if (b.y <= y && o < 0)
            --winding;
    }
    return winding != 0;
}
bool TerrainPolygonIndex::near(double x, double y, double slack) const {
    if (empty())
        return false;
    if (contains(x, y))
        return true;
    if (!(slack > 0))
        return false;
    bool found = false;
    const std::size_t n = polygon_.size();
    edges_.visit({x - slack, y - slack, x + slack, y + slack}, [&](std::uint32_t e) {
        found = pointSegment({x, y}, v2(polygon_[e]), v2(polygon_[(e + 1) % n])) <= slack;
        return !found;
    });
    return found;
}
bool TerrainPolygonIndex::segmentNear(TerrainVec2 a, TerrainVec2 b, double slack) const {
    if (empty())
        return false;
    if (near(a.x, a.y, slack) || near(b.x, b.y, slack))
        return true;
    const double s = std::max(slack, 0.0);
    const TerrainBoxTree::Box query{std::min(a.x, b.x) - s, std::min(a.y, b.y) - s,
                                    std::max(a.x, b.x) + s, std::max(a.y, b.y) + s};
    // A node whose box lies wholly (beyond rounding and the slack) on one side of the segment's
    // line holds no edge that meets it.
    const double dx = b.x - a.x, dy = b.y - a.y, length = planLength(dx, dy);
    const double margin = (s + 1e-9) * length + 1e-12 * (std::abs(dx) + std::abs(dy)) *
                                                    (std::abs(a.x) + std::abs(a.y) + 1);
    auto prune = [&](const TerrainBoxTree::Box &box) {
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const double x : {box[0], box[2]})
            for (const double y : {box[1], box[3]}) {
                const double side = dx * (y - a.y) - dy * (x - a.x);
                low = std::min(low, side);
                high = std::max(high, side);
            }
        return !(low > margin || high < -margin);
    };
    bool found = false;
    const std::size_t n = polygon_.size();
    edges_.visit(
        query,
        [&](std::uint32_t e) {
            const auto c = v2(polygon_[e]), d = v2(polygon_[(e + 1) % n]);
            found = s > 0 ? terrainSegmentDistance(a, b, c, d) <= s : terrainSegmentsMeet(a, b, c, d);
            return !found;
        },
        prune);
    return found;
}
} // namespace sketchy
