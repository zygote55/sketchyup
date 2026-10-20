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
bool protectedAt(const TerrainSourceState &state, double x, double y) {
    for (const auto &region : state.protectedRegions) {
        const auto &polygon = region.polygon;
        if (polygon.size() < 3)
            continue;
        double minX = polygon[0][0], minY = polygon[0][1], maxX = minX, maxY = minY;
        for (const auto &p : polygon) {
            minX = std::min(minX, p[0]);
            minY = std::min(minY, p[1]);
            maxX = std::max(maxX, p[0]);
            maxY = std::max(maxY, p[1]);
        }
        if (x < minX || x > maxX || y < minY || y > maxY)
            continue;
        if (inPolygon(polygon, x, y))
            return true;
    }
    return false;
}
void checkPoints(const std::vector<Plan2> &points, const char *what) {
    for (const auto &p : points)
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || std::abs(p[0]) > localBound ||
            std::abs(p[1]) > localBound)
            throw std::invalid_argument(std::string(what) +
                                        " points are finite and within 1,000 km of the terrain "
                                        "origin");
}
std::uint64_t cellKey(std::int64_t cx, std::int64_t cy) {
    return (std::uint64_t(std::uint32_t(std::int32_t(cx))) << 32) |
           std::uint64_t(std::uint32_t(std::int32_t(cy)));
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
        long double area = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto &a = data.region[i], &b = data.region[(i + 1) % n];
            if (a == b)
                throw std::invalid_argument("A region repeats a point; give each corner once");
            area += (long double)a[0] * b[1] - (long double)b[0] * a[1];
        }
        if (area == 0)
            throw std::invalid_argument("A region encloses no area");
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
    // position's band holds every edge that can count for it. The band of a Y is monotone.
    double low = polygon[0][1], high = low;
    for (const auto &p : polygon) {
        low = std::min(low, p[1]);
        high = std::max(high, p[1]);
    }
    std::size_t bands = std::min<std::size_t>(4096, n);
    bandLow_ = low;
    for (;;) {
        bandHeight_ = (high - low) / double(bands);
        if (!(bandHeight_ > 0)) {
            bands = 1;
            bandHeight_ = 1;
        }
        std::size_t entries = 0;
        auto bandOf = [&](double y) {
            const double t = std::floor((y - bandLow_) / bandHeight_);
            return std::size_t(std::clamp(t, 0.0, double(bands - 1)));
        };
        for (const auto &s : segments_)
            entries += bandOf(s.maxY) - bandOf(s.minY) + 1;
        if (entries <= 16 * n || bands == 1) {
            bands_.assign(bands, {});
            for (std::uint32_t e = 0; e < segments_.size(); ++e)
                for (auto b = bandOf(segments_[e].minY); b <= bandOf(segments_[e].maxY); ++b)
                    bands_[b].push_back(e);
            break;
        }
        bands = std::max<std::size_t>(1, bands / 2);
    }
    if (!(reach_ > 0))
        return;
    // A grid of edges for the distance outside: cells at least the falloff width, so a query
    // reads at most 3 x 3 cells. Each edge is listed in every cell it passes through (its box's
    // cells when few, otherwise samples at most half a cell apart with their neighbours).
    double perimeter = 0;
    for (const auto &s : segments_)
        perimeter += planLength(s.b[0] - s.a[0], s.b[1] - s.a[1]);
    cell_ = std::max(reach_, perimeter / double(n));
    originX_ = box_[0];
    originY_ = box_[1];
    auto cx = [&](double x) { return std::int64_t(std::floor((x - originX_) / cell_)); };
    auto cy = [&](double y) { return std::int64_t(std::floor((y - originY_) / cell_)); };
    for (std::uint32_t e = 0; e < segments_.size(); ++e) {
        const auto &s = segments_[e];
        const auto x0 = cx(s.minX), x1 = cx(s.maxX), y0 = cy(s.minY), y1 = cy(s.maxY);
        if ((x1 - x0 + 1) * (y1 - y0 + 1) <= 9) {
            for (auto x = x0; x <= x1; ++x)
                for (auto y = y0; y <= y1; ++y)
                    cells_.push_back({cellKey(x, y), e});
            continue;
        }
        const double length = planLength(s.b[0] - s.a[0], s.b[1] - s.a[1]);
        const auto steps = std::size_t(std::ceil(length / (cell_ / 2))) + 1;
        for (std::size_t k = 0; k <= steps; ++k) {
            const double t = double(k) / double(steps);
            const auto px = cx(s.a[0] + (s.b[0] - s.a[0]) * t),
                       py = cy(s.a[1] + (s.b[1] - s.a[1]) * t);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    cells_.push_back({cellKey(px + dx, py + dy), e});
        }
    }
    std::sort(cells_.begin(), cells_.end());
    cells_.erase(std::unique(cells_.begin(), cells_.end()), cells_.end());
}
bool TerrainSculptInfluence::inside(double x, double y) const {
    if (data_.brush() || bands_.empty())
        return false;
    const auto &polygon = data_.region;
    const std::size_t n = polygon.size();
    const double t = std::floor((y - bandLow_) / bandHeight_);
    if (t < 0 || t >= double(bands_.size()) + 1)
        return false; // below or above every edge
    const auto band = std::size_t(std::min(t, double(bands_.size() - 1)));
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
    // A segment whose box, widened by the reach, does not hold the position is farther than
    // the reach: skipping it never changes a distance below the reach.
    auto consider = [&](const Segment &s) {
        if (x < s.minX - reach_ || x > s.maxX + reach_ || y < s.minY - reach_ ||
            y > s.maxY + reach_)
            return;
        best = std::min(best, segmentDistance(x, y, s.a, s.b));
    };
    if (data_.brush() || cells_.empty()) {
        for (const auto &s : segments_)
            consider(s);
        return best;
    }
    const auto x0 = std::int64_t(std::floor((x - reach_ - originX_) / cell_)),
               x1 = std::int64_t(std::floor((x + reach_ - originX_) / cell_)),
               y0 = std::int64_t(std::floor((y - reach_ - originY_) / cell_)),
               y1 = std::int64_t(std::floor((y + reach_ - originY_) / cell_));
    for (auto cx = x0; cx <= x1; ++cx)
        for (auto cy = y0; cy <= y1; ++cy) {
            const auto key = cellKey(cx, cy);
            auto it = std::lower_bound(cells_.begin(), cells_.end(),
                                       std::pair<std::uint64_t, std::uint32_t>{key, 0});
            for (; it != cells_.end() && it->first == key; ++it)
                consider(segments_[it->second]);
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
TerrainSculptClip planTerrainSculptClip(const TerrainSourceState &state, const TerrainEdit &edit) {
    const auto &data = dataOf(edit);
    const TerrainSculptInfluence influence(data);
    const auto &box = influence.bounds();
    TerrainSculptClip clip;
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
               influence.weight(p.x, p.y) > 0 && !protectedAt(state, p.x, p.y);
    };
    for (const auto *source : state.sources()) {
        if (!lineRole(source->role) && source->role != TerrainSourceRole::Points)
            continue;
        const auto &points = source->points;
        std::vector<bool> keep;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (clipped(points[i])) {
                if (keep.empty())
                    keep.assign(points.size(), true);
                keep[i] = false;
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
    TerrainSculptPlan plan;
    plan.clip = planTerrainSculptClip(context.sources, edit);
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
    for (const auto *tile : mesh.tilesIn(box[0], box[1], box[2], box[3], 0))
        for (const auto &p : tile->content->vertices) {
            if (p.x < box[0] || p.x > box[2] || p.y < box[1] || p.y > box[3])
                continue;
            if (!seen.insert(TerrainPlanKey::of(p.x, p.y)).second)
                continue;
            const double w = influence.weight(p.x, p.y);
            if (!(w > 0))
                continue;
            ++plan.influenced;
            if (protectedAt(context.sources, p.x, p.y)) {
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
