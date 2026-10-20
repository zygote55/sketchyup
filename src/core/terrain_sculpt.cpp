#include "core/terrain_sculpt.hpp"
#include "core/terrain_edit_mesh.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
namespace sketchy {
namespace {
constexpr double localBound = 1e6;
using Plan2 = std::array<double, 2>;
const TerrainSculptData &dataOf(const TerrainEdit &edit) {
    return dynamic_cast<const TerrainSculptData &>(*edit.data);
}
bool planLess(const Plan2 &a, const Plan2 &b) { return std::tie(a[0], a[1]) < std::tie(b[0], b[1]); }
// A plan length by the correctly rounded square root (not hypot, whose last bit may differ
// between C libraries), so replay gives the same bits on every build. Coordinates are bounded
// (1e6 m), so the squares cannot overflow.
double planLength(double dx, double dy) { return std::sqrt(dx * dx + dy * dy); }
// The plan distance from p to the segment a-b with a <= b lexicographically: the end points
// exactly when the projection falls outside, so a shared end weighs alike from either segment.
double segmentDistance(double x, double y, const Plan2 &a, const Plan2 &b) {
    const double dx = b[0] - a[0], dy = b[1] - a[1];
    const double length = dx * dx + dy * dy;
    if (!(length > 0))
        return planLength(x - a[0], y - a[1]);
    const double t = ((x - a[0]) * dx + (y - a[1]) * dy) / length;
    if (t <= 0)
        return planLength(x - a[0], y - a[1]);
    if (t >= 1)
        return planLength(x - b[0], y - b[1]);
    return planLength(a[0] + t * dx - x, a[1] + t * dy - y);
}
int orient(const Plan2 &a, const Plan2 &b, double x, double y) {
    return terrainOrientation({a[0], a[1]}, {b[0], b[1]}, {x, y});
}
// Closed point-in-polygon by nonzero winding with the adapter's exact orientation.
bool inPolygon(const std::vector<Plan2> &polygon, double x, double y) {
    const std::size_t n = polygon.size();
    int winding = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto &a = polygon[i], &b = polygon[(i + 1) % n];
        const int o = orient(a, b, x, y);
        if (o == 0 && std::min(a[0], b[0]) <= x && x <= std::max(a[0], b[0]) &&
            std::min(a[1], b[1]) <= y && y <= std::max(a[1], b[1]))
            return true;
        if (a[1] <= y) {
            if (b[1] > y && o > 0)
                ++winding;
        } else if (b[1] <= y && o < 0)
            --winding;
    }
    return winding != 0;
}
// Pads (decision 6): on or inside a protected polygon.
// Pads (decision 6): on or inside a protected polygon. The polygons' boxes are computed once per
// plan, not per vertex.
class Pads {
  public:
    explicit Pads(const TerrainSourceState &state) {
        for (const auto &region : state.protectedRegions) {
            const auto &polygon = region.polygon;
            if (polygon.size() < 3)
                continue;
            std::array<double, 4> box{polygon[0][0], polygon[0][1], polygon[0][0], polygon[0][1]};
            for (const auto &p : polygon) {
                box[0] = std::min(box[0], p[0]);
                box[1] = std::min(box[1], p[1]);
                box[2] = std::max(box[2], p[0]);
                box[3] = std::max(box[3], p[1]);
            }
            pads_.push_back({box, &polygon});
        }
    }
    bool covers(double x, double y) const {
        for (const auto &[box, polygon] : pads_)
            if (x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3] &&
                inPolygon(*polygon, x, y))
                return true;
        return false;
    }

  private:
    std::vector<std::pair<std::array<double, 4>, const std::vector<Plan2> *>> pads_;
};
void checkPoints(const std::vector<Plan2> &points, const char *what) {
    for (const auto &p : points)
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || std::abs(p[0]) > localBound ||
            std::abs(p[1]) > localBound)
            throw std::invalid_argument(std::string(what) +
                                        " points are finite and within 1,000 km of the terrain "
                                        "origin");
}
bool lexLess(const Vec3 &a, const Vec3 &b) { return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z); }
bool lineRole(TerrainSourceRole role) {
    return role == TerrainSourceRole::Contour || role == TerrainSourceRole::Breakline;
}
bool closedLine(const TerrainSource &source) {
    return lineRole(source.role) && source.points.size() >= 4 &&
           source.points.front() == source.points.back();
}
} // namespace

const char *terrainFalloffName(TerrainFalloff falloff) {
    switch (falloff) {
    case TerrainFalloff::Smooth:
        return "smooth";
    case TerrainFalloff::Linear:
        return "linear";
    case TerrainFalloff::Constant:
        return "constant";
    }
    return "smooth";
}
std::optional<TerrainFalloff> terrainFalloffOf(const std::string &name) {
    for (auto falloff : {TerrainFalloff::Smooth, TerrainFalloff::Linear, TerrainFalloff::Constant})
        if (name == terrainFalloffName(falloff))
            return falloff;
    return std::nullopt;
}
double terrainFalloff(TerrainFalloff falloff, double t) {
    if (!(t < 1))
        return 0;
    if (!(t > 0))
        return 1;
    switch (falloff) {
    case TerrainFalloff::Smooth:
        return 1 - t * t * (3 - 2 * t);
    case TerrainFalloff::Linear:
        return 1 - t;
    case TerrainFalloff::Constant:
        return 1;
    }
    return 0;
}

bool TerrainSculptData::equals(const TerrainEditData &other) const {
    const auto *o = dynamic_cast<const TerrainSculptData *>(&other);
    return o && o->path == path && o->radius == radius && o->region == region &&
           o->width == width && o->falloff == falloff && o->amount == amount &&
           o->strength == strength && o->iterations == iterations && o->pieces == pieces;
}
std::size_t TerrainSculptData::bytes() const {
    return sizeof(TerrainSculptData) + (path.size() + region.size()) * sizeof(Plan2) +
           pieces.size() * sizeof(pieces[0]);
}
void validateTerrainSculptData(const std::string &kind, const TerrainSculptData &data) {
    const bool sculpt = kind == terrainSculptKind;
    if (!sculpt && kind != terrainSmoothKind)
        throw std::invalid_argument("Unknown terrain sculpt kind");
    if (data.path.empty() == data.region.empty())
        throw std::invalid_argument("A sculpt has either a brush path or a region");
    if (data.brush()) {
        if (data.path.size() > terrainSculptPathPoints)
            throw std::invalid_argument("A brush path has 1 to 256 points");
        checkPoints(data.path, "Brush");
        if (!std::isfinite(data.radius) || data.radius < terrainSculptRadiusMin ||
            data.radius > terrainSculptRadiusMax)
            throw std::invalid_argument("The brush radius is 0.01 to 1,000 m");
        if (data.width != 0)
            throw std::invalid_argument("A brush has a radius, not a falloff width");
    } else {
        const auto n = data.region.size();
        if (n < 3 || n > terrainSculptRegionPoints)
            throw std::invalid_argument("A region has 3 to 20,000 points");
        checkPoints(data.region, "Region");
        for (std::size_t i = 0; i < n; ++i)
            if (data.region[i] == data.region[(i + 1) % n])
                throw std::invalid_argument("A region repeats a point; give each corner once");
        // Some corner off the line of the first edge (the adapter's exact orientation, so the
        // answer is the same on every platform).
        bool area = false;
        for (std::size_t k = 2; k < n && !area; ++k)
            area = orient(data.region[0], data.region[1], data.region[k][0], data.region[k][1]) != 0;
        if (!area)
            throw std::invalid_argument("A region encloses no area (its corners are on one line)");
        if (terrainSculptRegionBands(data.region).edges > terrainSculptBandEdges)
            throw std::invalid_argument("A region's outline crosses one horizontal band more than "
                                        "512 times; split it into simpler regions");
        if (!std::isfinite(data.width) || data.width < 0 || data.width > terrainSculptWidthMax)
            throw std::invalid_argument("The falloff width is 0 to 1,000 m");
        if (data.radius != 0)
            throw std::invalid_argument("A region has a falloff width, not a radius");
    }
    if (data.pieces.size() > terrainSculptPiecesPerEdit)
        throw std::invalid_argument("A sculpt records at most 4,096 source line pieces");
    {
        std::set<std::uint32_t> ids;
        for (const auto &[parent, id] : data.pieces)
            if (!parent || id < terrainSplitSourceIdFloor || !ids.insert(id).second)
                throw std::invalid_argument("A sculpt's source line pieces name a source and "
                                            "distinct piece ids from 3,221,225,472 up");
    }
    if (data.falloff != TerrainFalloff::Smooth && data.falloff != TerrainFalloff::Linear &&
        data.falloff != TerrainFalloff::Constant)
        throw std::invalid_argument("The falloff is smooth, linear or constant");
    if (sculpt) {
        if (!std::isfinite(data.amount) || data.amount == 0 ||
            std::abs(data.amount) > terrainSculptAmountMax)
            throw std::invalid_argument("The amount is a non-zero height of at most 1,000 m "
                                        "(negative lowers)");
        if (data.strength != 0 || data.iterations != 0)
            throw std::invalid_argument("Strength and iterations apply to smoothing");
    } else {
        if (!std::isfinite(data.strength) || !(data.strength > 0) || data.strength > 1)
            throw std::invalid_argument("The smoothing strength is above 0 and at most 1");
        if (data.iterations < 1 || data.iterations > terrainSmoothIterationsMax)
            throw std::invalid_argument("Smoothing runs 1 to 8 iterations");
        if (data.amount != 0)
            throw std::invalid_argument("An amount applies to raising and lowering");
    }
}

std::array<double, 4> terrainSculptBounds(const TerrainSculptData &data) {
    const auto &points = data.brush() ? data.path : data.region;
    const double reach = data.brush() ? data.radius : data.width;
    if (points.empty())
        return {0, 0, 0, 0};
    std::array<double, 4> box{points[0][0], points[0][1], points[0][0], points[0][1]};
    for (const auto &p : points) {
        box[0] = std::min(box[0], p[0]);
        box[1] = std::min(box[1], p[1]);
        box[2] = std::max(box[2], p[0]);
        box[3] = std::max(box[3], p[1]);
    }
    return {box[0] - reach, box[1] - reach, box[2] + reach, box[3] + reach};
}

// ---- Influence ------------------------------------------------------------------------------
TerrainSculptBands terrainSculptRegionBands(const std::vector<Plan2> &region) {
    TerrainSculptBands bands;
    const std::size_t n = region.size();
    if (n < 3)
        return bands;
    double low = region[0][1], high = low;
    for (const auto &p : region) {
        low = std::min(low, p[1]);
        high = std::max(high, p[1]);
    }
    bands.count = std::min<std::size_t>(4096, n);
    bands.low = low;
    bands.height = (high - low) / double(bands.count);
    if (!(bands.height > 0)) {
        bands.count = 1;
        bands.height = 1;
    }
    // Edges per band by a difference array: no band list is built for a hostile region.
    std::vector<std::int64_t> delta(bands.count + 1, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const auto &a = region[i], &b = region[(i + 1) % n];
        const auto lo = terrainSculptBandOf(bands, std::min(a[1], b[1])),
                   hi = terrainSculptBandOf(bands, std::max(a[1], b[1]));
        ++delta[lo];
        --delta[hi + 1];
    }
    std::int64_t running = 0;
    for (std::size_t band = 0; band < bands.count; ++band) {
        running += delta[band];
        if (std::size_t(running) > bands.edges) {
            bands.edges = std::size_t(running);
            bands.busiest = band;
        }
    }
    return bands;
}
std::size_t terrainSculptBandOf(const TerrainSculptBands &bands, double y) {
    const double t = std::floor((y - bands.low) / bands.height);
    return std::size_t(std::clamp(t, 0.0, double(bands.count - 1)));
}
TerrainSculptInfluence::TerrainSculptInfluence(const TerrainSculptData &data) : data_(data) {
    box_ = terrainSculptBounds(data);
    reach_ = data.brush() ? data.radius : data.width;
    auto addSegment = [&](Plan2 a, Plan2 b) {
        if (planLess(b, a))
            std::swap(a, b);
        segments_.push_back({a, b, a[0], std::min(a[1], b[1]), b[0], std::max(a[1], b[1])});
    };
    if (data.brush()) {
        // Distinct consecutive points only, so repeated samples change nothing; a click (or a
        // path of one repeated point) is one point.
        for (std::size_t i = 1; i < data.path.size(); ++i)
            if (data.path[i] != data.path[i - 1])
                addSegment(data.path[i - 1], data.path[i]);
        if (segments_.empty())
            addSegment(data.path[0], data.path[0]);
        return;
    }
    const auto &polygon = data.region;
    const std::size_t n = polygon.size();
    if (n < 3)
        return;
    for (std::size_t i = 0; i < n; ++i)
        addSegment(polygon[i], polygon[(i + 1) % n]);
    // Bands for the winding test: every edge is listed in the bands its Y range meets, so a
    // position's band holds every edge that can count for it (the band of a Y is monotone). A
    // valid region has at most terrainSculptBandEdges edges a band; an unvalidated one over it
    // gets one band (correct, not bounded).
    auto bands = terrainSculptRegionBands(polygon);
    if (bands.edges > terrainSculptBandEdges) {
        bands.count = 1;
        bands.height = std::numeric_limits<double>::infinity();
    }
    bandLow_ = bands.low;
    bandHeight_ = bands.height;
    bands_.assign(bands.count, {});
    for (std::uint32_t e = 0; e < n; ++e) {
        const auto &a = polygon[e], &b = polygon[(e + 1) % n];
        const auto lo = terrainSculptBandOf(bands, std::min(a[1], b[1])),
                   hi = terrainSculptBandOf(bands, std::max(a[1], b[1]));
        for (auto band = lo; band <= hi; ++band)
            bands_[band].push_back(e);
    }
    if (!(reach_ > 0))
        return;
    // A bounding-volume tree of the edges for the distance outside: a nearest-edge search with
    // pruning reads a few leaves near the position, whatever the region's shape.
    order_.resize(segments_.size());
    for (std::uint32_t e = 0; e < order_.size(); ++e)
        order_[e] = e;
    nodes_.reserve(2 * segments_.size() / 4 + 2);
    build(0, std::uint32_t(segments_.size()));
}
std::uint32_t TerrainSculptInfluence::build(std::uint32_t first, std::uint32_t count) {
    const auto index = std::uint32_t(nodes_.size());
    nodes_.push_back({});
    Node node{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
              first, count, 0, 0};
    for (std::uint32_t i = first; i < first + count; ++i) {
        const auto &s = segments_[order_[i]];
        node.minX = std::min(node.minX, s.minX);
        node.minY = std::min(node.minY, s.minY);
        node.maxX = std::max(node.maxX, s.maxX);
        node.maxY = std::max(node.maxY, s.maxY);
    }
    if (count > 4) {
        // Split at the median centre along the longer side (ties by edge index). The tree only
        // orders the search; the distance is the minimum over the edges either way.
        const bool alongX = node.maxX - node.minX >= node.maxY - node.minY;
        auto centre = [&](std::uint32_t e) {
            const auto &s = segments_[e];
            return alongX ? s.a[0] + s.b[0] : s.a[1] + s.b[1];
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
bool TerrainSculptInfluence::inside(double x, double y) const {
    if (data_.brush() || bands_.empty())
        return false;
    const auto &polygon = data_.region;
    const std::size_t n = polygon.size();
    const double t = std::floor((y - bandLow_) / bandHeight_);
    const auto band = std::size_t(std::clamp(t, 0.0, double(bands_.size() - 1)));
    int winding = 0;
    for (const auto e : bands_[band]) {
        const auto &a = polygon[e], &b = polygon[(e + 1) % n];
        if (y < std::min(a[1], b[1]) || y > std::max(a[1], b[1]))
            continue;
        const int o = orient(a, b, x, y);
        if (o == 0 && std::min(a[0], b[0]) <= x && x <= std::max(a[0], b[0]))
            return true;
        if (a[1] <= y) {
            if (b[1] > y && o > 0)
                ++winding;
        } else if (b[1] <= y && o < 0)
            --winding;
    }
    return winding != 0;
}
double TerrainSculptInfluence::distance(double x, double y) const {
    double best = std::numeric_limits<double>::infinity();
    if (nodes_.empty()) {
        // A brush (at most 255 segments): a segment whose box, widened by the reach, does not
        // hold the position is farther than the reach, so skipping it never changes a distance
        // below the reach.
        for (const auto &s : segments_) {
            if (x < s.minX - reach_ || x > s.maxX + reach_ || y < s.minY - reach_ ||
                y > s.maxY + reach_)
                continue;
            best = std::min(best, segmentDistance(x, y, s.a, s.b));
        }
        return best;
    }
    // A node is skipped only when its box is clearly farther (beyond rounding) than both the best
    // distance so far and the reach, so every edge that could give the minimum below the reach,
    // to the last bit, is measured: the result does not depend on the tree's shape.
    auto beyond = [](double lowerBound, double limit) {
        return lowerBound > limit * (1 + 1e-9) + 1e-12;
    };
    std::uint32_t stack[64];
    std::size_t depth = 0;
    stack[depth++] = 0;
    while (depth) {
        const auto &node = nodes_[stack[--depth]];
        const double dx = std::max({node.minX - x, 0.0, x - node.maxX}),
                     dy = std::max({node.minY - y, 0.0, y - node.maxY});
        if (beyond(planLength(dx, dy), std::min(best, reach_)))
            continue;
        if (node.count) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const auto &s = segments_[order_[i]];
                best = std::min(best, segmentDistance(x, y, s.a, s.b));
            }
            continue;
        }
        stack[depth++] = node.right;
        stack[depth++] = node.left;
    }
    return best;
}
double TerrainSculptInfluence::weight(double x, double y) const {
    if (x < box_[0] || x > box_[2] || y < box_[1] || y > box_[3])
        return 0;
    if (!data_.brush() && inside(x, y))
        return 1;
    if (!(reach_ > 0))
        return 0;
    const double d = distance(x, y);
    if (!(d < reach_))
        return 0;
    return terrainFalloff(data_.falloff, d / reach_);
}

std::optional<std::uint32_t> terrainSplitSourceId(std::uint32_t edit, std::uint32_t piece) {
    if (!edit || edit > terrainSculptSplitEdits || piece >= terrainSculptPiecesPerEdit)
        return std::nullopt;
    return std::uint32_t(0xFFFFFFFFu - (edit - 1) * terrainSculptPiecesPerEdit - piece);
}

// ---- Clipping (decision 2) ------------------------------------------------------------------
TerrainSculptClip planTerrainSculptClip(const TerrainSourceState &state, const TerrainEdit &edit,
                                        TerrainProgress *progress) {
    const auto &data = dataOf(edit);
    const TerrainSculptInfluence influence(data);
    const auto &box = influence.bounds();
    const Pads pads(state);
    TerrainSculptClip clip;
    // Recorded pieces belong to this entry's block (validateTerrainRecord refuses others in a
    // file); one that does not is refused here too rather than used.
    for (const auto &[parent, id] : data.pieces) {
        const auto high = terrainSplitSourceId(edit.id, 0),
                   low = terrainSplitSourceId(edit.id, terrainSculptPiecesPerEdit - 1);
        if (!high || id > *high || id < *low) {
            clip.refusal = "A recorded source line piece id is not this change's";
            return clip;
        }
    }
    // Ids of further runs: a parent's runs first take the ids the entry recorded for that parent
    // at its commit (in order), so a piece keeps its id while its parent splits the same way,
    // whatever happens to other sources; any further run takes the next ordinal of the entry's
    // block that no recorded id uses.
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> recorded;
    std::unordered_set<std::uint32_t> reserved;
    for (const auto &[parent, id] : data.pieces) {
        recorded[parent].push_back(id);
        reserved.insert(id);
    }
    std::uint32_t ordinal = 0;
    auto fresh = [&]() -> std::optional<std::uint32_t> {
        for (;;) {
            const auto id = terrainSplitSourceId(edit.id, ordinal++);
            if (!id || !reserved.contains(*id))
                return id;
        }
    };
    std::set<std::uint32_t> taken;
    auto clipped = [&](const Vec3 &p) {
        return p.x >= box[0] && p.x <= box[2] && p.y >= box[1] && p.y <= box[3] &&
               influence.weight(p.x, p.y) > 0 && !pads.covers(p.x, p.y);
    };
    std::size_t scanned = 0;
    for (const auto *source : state.sources()) {
        if (!lineRole(source->role) && source->role != TerrainSourceRole::Points)
            continue;
        const auto &points = source->points;
        std::vector<bool> keep;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (++scanned % 4096 == 0)
                terrainCheckpoint(progress, TerrainStage::Replay, 0); // cancellation only
            if (clipped(points[i])) {
                if (keep.empty())
                    keep.assign(points.size(), true);
                keep[i] = false;
            }
        }
        if (keep.empty())
            continue;
        const bool closed = closedLine(*source);
        TerrainSculptClip::Source entry{source->id, source->role, {}, {}};
        for (std::size_t i = 0; i < points.size() - (closed ? 1 : 0); ++i)
            clip.vertices += keep[i] ? 0 : 1;
        if (source->role == TerrainSourceRole::Points) {
            if (std::none_of(keep.begin(), keep.end(), [](bool k) { return k; }))
                ++clip.removed;
            entry.keep = std::move(keep);
            clip.sources.push_back(std::move(entry));
            continue;
        }
        // The runs of kept vertices, in point order; a ring is walked from its first clipped
        // vertex, so a run never wraps.
        std::vector<std::vector<Vec3>> runs(1);
        auto visit = [&](std::size_t i) {
            if (keep[i])
                runs.back().push_back(points[i]);
            else if (!runs.back().empty())
                runs.emplace_back();
        };
        if (closed) {
            const std::size_t m = points.size() - 1;
            std::size_t first = 0;
            while (keep[first])
                ++first;
            for (std::size_t j = 1; j < m; ++j)
                visit((first + j) % m);
        } else
            for (std::size_t i = 0; i < points.size(); ++i)
                visit(i);
        for (auto &run : runs) {
            if (run.size() < 2)
                continue; // a single vertex is no line
            if (lexLess(run.back(), run.front()))
                std::reverse(run.begin(), run.end());
            TerrainSource next;
            next.role = source->role;
            next.reference = source->reference;
            next.points = std::move(run);
            if (entry.pieces.empty())
                next.id = source->id;
            else {
                const auto further = entry.pieces.size() - 1;
                const auto found = recorded.find(source->id);
                const auto id = found != recorded.end() && further < found->second.size()
                                    ? std::optional<std::uint32_t>(found->second[further])
                                    : fresh();
                ++clip.pieces;
                if (!id) {
                    clip.refusal = edit.id > terrainSculptSplitEdits
                                       ? "The terrain has used too many change IDs to split "
                                         "source lines; Explode terrain keeps the surface as "
                                         "ordinary geometry"
                                       : "This change would split source lines into more than "
                                         "4,096 pieces; sculpt or smooth in smaller strokes";
                    return clip;
                }
                if (state.find(*id) || !taken.insert(*id).second) {
                    clip.refusal = "A source line piece would take the id " +
                                   std::to_string(*id) + ", which a source already has";
                    return clip;
                }
                next.id = *id;
                clip.resolved.push_back({source->id, *id});
            }
            entry.pieces.push_back(std::move(next));
        }
        if (entry.pieces.empty())
            ++clip.removed;
        clip.sources.push_back(std::move(entry));
    }
    return clip;
}
void applyTerrainSculptClip(TerrainSourceState &state, const TerrainSculptClip &clip) {
    if (!clip.refusal.empty())
        return;
    for (const auto &source : clip.sources) {
        if (source.role == TerrainSourceRole::Points) {
            state.clipPoints(source.id, source.keep);
            continue;
        }
        if (source.pieces.empty()) {
            state.remove(source.id);
            continue;
        }
        state.replace(source.pieces.front());
        for (std::size_t i = 1; i < source.pieces.size(); ++i)
            state.add(source.pieces[i]);
    }
}

// ---- The mesh change ------------------------------------------------------------------------
TerrainSculptPlan planTerrainSculpt(const TerrainEditContext &context, const TerrainEdit &edit) {
    const auto &data = dataOf(edit);
    const auto &mesh = context.mesh;
    const TerrainSculptInfluence influence(data);
    const auto &box = influence.bounds();
    const Pads pads(context.sources);
    TerrainSculptPlan plan;
    plan.clip = planTerrainSculptClip(context.sources, edit, context.progress);
    if (!plan.clip.refusal.empty()) {
        plan.refusal = plan.clip.refusal;
        plan.refusalCode = "LIMIT_EXCEEDED";
        return plan;
    }
    // The vertices with a weight, each position once, in (x, y) order.
    struct Moving {
        Vec3 p;
        double w;
    };
    std::vector<Moving> moving;
    std::unordered_set<TerrainPlanKey, TerrainPlanHash> seen;
    std::size_t scanned = 0;
    for (const auto *tile : mesh.tilesIn(box[0], box[1], box[2], box[3], 0))
        for (const auto &p : tile->content->vertices) {
            if (++scanned % 4096 == 0)
                terrainCheckpoint(context.progress, TerrainStage::Replay, 0); // cancellation only
            if (p.x < box[0] || p.x > box[2] || p.y < box[1] || p.y > box[3])
                continue;
            if (!seen.insert(TerrainPlanKey::of(p.x, p.y)).second)
                continue;
            const double w = influence.weight(p.x, p.y);
            if (!(w > 0))
                continue;
            ++plan.influenced;
            if (pads.covers(p.x, p.y)) {
                ++plan.protectedVertices;
                continue;
            }
            moving.push_back({p, w});
        }
    std::sort(moving.begin(), moving.end(), [](const Moving &a, const Moving &b) {
        return std::tie(a.p.x, a.p.y) < std::tie(b.p.x, b.p.y);
    });
    auto &lifted = plan.change.lifted;
    if (edit.kind == terrainSculptKind) {
        for (const auto &v : moving) {
            const double z = v.p.z + data.amount * v.w;
            if (z != v.p.z)
                lifted.push_back({v.p, z});
        }
    } else {
        // Jacobi smoothing over the moving vertices (indices 0..n-1) and their one-ring
        // neighbours (indices from n on, read only).
        const std::size_t n = moving.size();
        std::unordered_map<TerrainPlanKey, std::uint32_t, TerrainPlanHash> index;
        index.reserve(n * 2);
        std::vector<double> z;
        z.reserve(n * 2);
        for (std::size_t i = 0; i < n; ++i) {
            index.emplace(TerrainPlanKey::of(moving[i].p.x, moving[i].p.y), std::uint32_t(i));
            z.push_back(moving[i].p.z);
        }
        std::vector<std::vector<std::pair<std::uint32_t, double>>> ring(n);
        std::vector<Vec3> around;
        for (std::size_t i = 0; i < n; ++i) {
            if ((i + 1) % 4096 == 0)
                terrainCheckpoint(context.progress, TerrainStage::Replay, 0);
            const auto &p = moving[i].p;
            around.clear();
            for (const auto &ref : mesh.trianglesAt(p)) {
                const auto &content = *mesh.tile(ref.tile)->content;
                for (const auto corner : content.triangles[ref.triangle]) {
                    const auto &q = content.vertices[corner];
                    if (q.x != p.x || q.y != p.y)
                        around.push_back(q);
                }
            }
            std::sort(around.begin(), around.end(), [](const Vec3 &a, const Vec3 &b) {
                return std::tie(a.x, a.y) < std::tie(b.x, b.y);
            });
            around.erase(std::unique(around.begin(), around.end(),
                                     [](const Vec3 &a, const Vec3 &b) {
                                         return a.x == b.x && a.y == b.y;
                                     }),
                         around.end());
            for (const auto &q : around) {
                const auto [it, inserted] =
                    index.try_emplace(TerrainPlanKey::of(q.x, q.y), std::uint32_t(z.size()));
                if (inserted)
                    z.push_back(q.z);
                ring[i].push_back({it->second, 1 / planLength(q.x - p.x, q.y - p.y)});
            }
        }
        std::vector<double> next = z;
        for (int iteration = 0; iteration < data.iterations; ++iteration) {
            terrainCheckpoint(context.progress, TerrainStage::Replay, 0);
            for (std::size_t i = 0; i < n; ++i) {
                if (ring[i].empty())
                    continue;
                double sum = 0, weights = 0;
                for (const auto &[j, w] : ring[i]) {
                    sum += w * z[j];
                    weights += w;
                }
                next[i] = z[i] + data.strength * moving[i].w * (sum / weights - z[i]);
            }
            z.swap(next);
        }
        for (std::size_t i = 0; i < n; ++i)
            if (z[i] != moving[i].p.z)
                lifted.push_back({moving[i].p, z[i]});
    }
    if (lifted.empty()) {
        plan.refusalCode = "INVALID_REQUEST";
        plan.refusal =
            plan.protectedVertices && plan.protectedVertices == plan.influenced
                ? "Every terrain vertex under the change is on a pad, which sculpt and smooth do "
                  "not move"
            : plan.influenced ? "The change moves no terrain vertex (the surface there is already "
                                "as asked)"
                              : "The change reaches no terrain vertex; a brush smaller than the "
                                "spacing of the terrain's vertices moves nothing";
        plan.clip = {};
    }
    return plan;
}

std::vector<TerrainEditKind> terrainSculptKinds() {
    std::vector<TerrainEditKind> kinds;
    for (const char *name : {terrainSculptKind, terrainSmoothKind}) {
        TerrainEditKind kind;
        kind.name = name;
        kind.applyInContext = [](const TerrainEditContext &context, const TerrainEdit &edit) {
            auto plan = planTerrainSculpt(context, edit);
            if (!plan.refusal.empty())
                return TerrainEditOutcome{std::nullopt, plan.refusal};
            return TerrainEditOutcome{std::move(plan.change), {}};
        };
        kind.sources = [](TerrainSourceState &state, const TerrainEdit &edit) {
            applyTerrainSculptClip(state, planTerrainSculptClip(state, edit));
        };
        kind.bounds = [](const TerrainEdit &edit) { return terrainSculptBounds(dataOf(edit)); };
        kinds.push_back(std::move(kind));
    }
    return kinds;
}
} // namespace sketchy
