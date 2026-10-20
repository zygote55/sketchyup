#include "core/terrain_drape.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_mesh_view.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_source_edit.hpp"
#include <algorithm>
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
using Plan2 = std::array<double, 2>;
using Box = std::array<double, 4>;
constexpr double localBound = 1e6;
TerrainVec2 v2(const Vec3 &p) { return {p.x, p.y}; }
TerrainVec2 v2(const Plan2 &p) { return {p[0], p[1]}; }
int orient(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c) { return terrainOrientation(a, b, c); }
// Lengths by the correctly rounded square root (not hypot), so no C library's last bit enters
// replay; coordinates are bounded, so the squares cannot overflow.
double planLength(double dx, double dy) { return std::sqrt(dx * dx + dy * dy); }
// -0.0 is stored as +0.0 (Qt's JSON writes it as 0, so a reopened entry would differ).
double clean(double v) { return v + 0.0; }
bool lexLess(TerrainVec2 a, TerrainVec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); }
bool lexLess(const Vec3 &a, const Vec3 &b) { return lexLess(v2(a), v2(b)); }
constexpr auto lexLess2 = [](TerrainVec2 a, TerrainVec2 b) { return lexLess(a, b); };
TerrainPlanKey keyOf(const Vec3 &p) { return TerrainPlanKey::of(p.x, p.y); }
TerrainPlanKey keyOf(TerrainVec2 p) { return TerrainPlanKey::of(p.x, p.y); }
const TerrainDrapeData &dataOf(const TerrainEdit &edit) {
    return dynamic_cast<const TerrainDrapeData &>(*edit.data);
}
Box emptyBox() {
    return {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
}
void extend(Box &box, double x, double y) {
    box[0] = std::min(box[0], x);
    box[1] = std::min(box[1], y);
    box[2] = std::max(box[2], x);
    box[3] = std::max(box[3], y);
}
bool finitePoint(double x, double y) {
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= localBound &&
           std::abs(y) <= localBound;
}
// The plan distance from p to the closed segment, measured from the segment's lexicographically
// smaller end, so a segment and its reverse give the same bits; `param` receives the projection's
// parameter from that end (clamped to [0, 1]).
double segmentDistance(TerrainVec2 p, TerrainVec2 a, TerrainVec2 b, double *param = nullptr) {
    if (lexLess(b, a))
        std::swap(a, b);
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    if (param)
        *param = t;
    return planLength(a.x + t * dx - p.x, a.y + t * dy - p.y);
}

// ---- Problems (the import's report kinds, located in the terrain frame) ----------------------
class Problems {
  public:
    explicit Problems(std::string what) : what_(std::move(what)) {}
    void add(const std::string &kind, const std::string &message, TerrainVec2 xy,
             std::uint64_t a = 0, std::uint64_t b = 0) {
        auto &entry = find(kind, message);
        ++entry.count;
        if (entry.locations.size() < terrainMaxLocationsPerKind)
            entry.locations.push_back({xy, a, b});
    }
    void merge(const TerrainProblem &problem) {
        auto &entry = find(terrainProblemKindName(problem.kind), problem.message);
        entry.count += problem.count;
        for (const auto &location : problem.locations)
            if (entry.locations.size() < terrainMaxLocationsPerKind)
                entry.locations.push_back(location);
    }
    bool any() const { return !list_.empty(); }
    [[noreturn]] void refuse() const {
        bool limit = false;
        std::string kinds;
        for (const auto &problem : list_) {
            limit = limit || problem.kind == "limit-exceeded" || problem.kind == "triangle-budget" ||
                    problem.kind == "extent-exceeded";
            if (!kinds.empty())
                kinds += ", ";
            kinds += problem.kind + " (" + std::to_string(problem.count) + ")";
        }
        throw TerrainEditRefused(limit ? "LIMIT_EXCEEDED" : "INVALID_REQUEST",
                                 (what_ + " refused; nothing was changed: " + kinds).substr(0, 256),
                                 list_);
    }
    void check() const {
        if (any())
            refuse();
    }
    [[noreturn]] void refuse(const std::string &kind, const std::string &message, TerrainVec2 xy,
                             std::uint64_t a = 0, std::uint64_t b = 0) {
        add(kind, message, xy, a, b);
        refuse();
    }

  private:
    TerrainEditProblem &find(const std::string &kind, const std::string &message) {
        for (auto &problem : list_)
            if (problem.kind == kind)
                return problem;
        list_.push_back({kind, message.substr(0, terrainMaxMessageBytes), 0, {}});
        return list_.back();
    }
    std::string what_;
    std::vector<TerrainEditProblem> list_;
};
constexpr const char *limitKind = "limit-exceeded";
constexpr const char *splitKind = "triangulation-failed";
constexpr const char *splitMessage =
    "The draped line could not be split exactly here (it runs within rounding of the mesh's "
    "vertices or edges); move it slightly.";
void checkpoint(TerrainProgress *progress) {
    terrainCheckpoint(progress, TerrainStage::Replay, 0); // cancellation only
}
} // namespace

// ---- Data -----------------------------------------------------------------------------------
bool TerrainDrapeData::equals(const TerrainEditData &other) const {
    const auto *o = dynamic_cast<const TerrainDrapeData *>(&other);
    return o && o->lines == lines && o->paint == paint && o->runs == runs && o->splits == splits;
}
std::size_t TerrainDrapeData::bytes() const {
    std::size_t n = sizeof(TerrainDrapeData);
    for (const auto &line : lines)
        n += sizeof(line) + line.points.size() * sizeof(Plan2);
    for (const auto &region : paint) {
        n += sizeof(region);
        for (const auto &loop : region.loops)
            n += sizeof(loop) + loop.size() * sizeof(Plan2);
    }
    for (const auto &run : runs)
        n += sizeof(run) + run.size() * sizeof(Vec3);
    for (const auto &split : splits)
        n += sizeof(split) + split.points.size() * sizeof(Vec3);
    return n;
}
std::vector<Id> TerrainDrapeData::materials() const {
    std::vector<Id> list;
    for (const auto &region : paint)
        list.push_back(region.material);
    return list;
}
void validateTerrainDrapeData(const TerrainDrapeData &data) {
    auto fail = [](const std::string &message) { throw std::invalid_argument(message); };
    if (data.lines.empty() || data.lines.size() > terrainDrapeLines)
        fail("A drape has 1 to 4,096 lines");
    std::size_t points = 0;
    auto checkLoop = [&](const std::vector<Plan2> &line, bool closed, const char *what) {
        if (line.size() < (closed ? 3u : 2u))
            fail(std::string(what) + " has at least two points (three when closed)");
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (!finitePoint(line[i][0], line[i][1]))
                fail("Drape points are finite and within 1,000,000 m of the terrain");
            if (i && line[i] == line[i - 1])
                fail(std::string(what) + " repeats a point");
        }
        if (closed && line.front() == line.back())
            fail(std::string(what) + " repeats its first point");
        points += line.size();
        if (points > terrainDrapePoints)
            fail("A drape has at most 20,000 points");
    };
    for (const auto &line : data.lines)
        checkLoop(line.points, line.closed, "A drape line");
    if (data.paint.size() > terrainDrapePaintRegions)
        fail("A drape has at most 1,024 paint regions");
    std::size_t paintPoints = 0;
    for (const auto &region : data.paint) {
        if (region.loops.empty() || !region.material)
            fail("A drape's paint region has loops and a material");
        for (const auto &loop : region.loops) {
            const auto before = points;
            checkLoop(loop, true, "A paint loop");
            paintPoints += points - before;
            points = before;
            if (paintPoints > terrainDrapePoints)
                fail("A drape's paint regions have at most 20,000 points");
            if (terrainSculptRegionBands(loop).edges > terrainSculptBandEdges)
                fail("A paint loop has at most 512 edges in one horizontal band");
        }
    }
    if (data.runs.size() > terrainDrapeRuns)
        fail("A drape has at most 4,096 runs");
    std::size_t resolved = 0;
    for (const auto &run : data.runs) {
        if (run.size() < 2)
            fail("A drape run has at least two points");
        for (std::size_t i = 0; i < run.size(); ++i) {
            const auto &p = run[i];
            if (!finitePoint(p.x, p.y) || !std::isfinite(p.z) || std::abs(p.z) > localBound)
                fail("Drape runs are finite and within 1,000,000 m of the terrain");
            if (i && p.x == run[i - 1].x && p.y == run[i - 1].y)
                fail("A drape run repeats a point");
        }
        resolved += run.size();
        if (resolved > terrainDrapeResolvedPoints)
            fail("A drape's runs have at most 65,536 points");
    }
    if (!data.splits.empty() && data.runs.empty())
        fail("A drape that did not apply splits no source");
    std::size_t splitPoints = 0;
    std::set<std::pair<Plan2, Plan2>> segments;
    for (const auto &split : data.splits) {
        if (!finitePoint(split.a[0], split.a[1]) || !finitePoint(split.b[0], split.b[1]) ||
            !lexLess(v2(split.a), v2(split.b)) || split.points.empty())
            fail("A drape split names a segment (smaller end first) and its points");
        if (!segments.insert({split.a, split.b}).second)
            fail("A drape splits a segment once");
        for (const auto &p : split.points)
            if (!finitePoint(p.x, p.y) || !std::isfinite(p.z) || std::abs(p.z) > localBound)
                fail("Drape split points are finite and within 1,000,000 m of the terrain");
        splitPoints += split.points.size();
        if (splitPoints > terrainDrapeSplitPoints)
            fail("A drape splits sources at most 65,536 times");
    }
}
std::array<double, 4> terrainDrapeBounds(const TerrainDrapeData &data) {
    auto box = emptyBox();
    for (const auto &line : data.lines)
        for (const auto &p : line.points)
            extend(box, p[0], p[1]);
    for (const auto &region : data.paint)
        for (const auto &loop : region.loops)
            for (const auto &p : loop)
                extend(box, p[0], p[1]);
    if (!(box[0] <= box[2]))
        return {0, 0, 0, 0};
    return box;
}

// ---- The plan ---------------------------------------------------------------------------------
namespace {
// 1. The lines made to meet only at shared points.
struct Arranged {
    std::vector<TerrainVec2> points;
    std::vector<std::vector<std::uint32_t>> lines; // point indices; a closed line does not repeat
    std::vector<bool> closed;
    std::vector<std::uint32_t> source; // the input line of each arranged line
    std::size_t intersections{};
};
struct CellKey {
    std::int64_t x{}, y{};
    auto operator<=>(const CellKey &) const = default;
    bool operator==(const CellKey &) const = default;
};
struct CellHash {
    std::size_t operator()(const CellKey &k) const {
        return std::size_t(std::uint64_t(k.x) * 0x9E3779B97F4A7C15ULL ^ std::uint64_t(k.y));
    }
};
Arranged arrange(const TerrainDrapeData &data, Problems &problems, TerrainProgress *progress) {
    Arranged result;
    // Unique input points, lexicographic.
    std::vector<TerrainVec2> unique;
    for (const auto &line : data.lines)
        for (const auto &p : line.points)
            unique.push_back({clean(p[0]), clean(p[1])});
    std::sort(unique.begin(), unique.end(), lexLess2);
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    // Points within 1e-7 m of each other are one point: the components of the "within" relation,
    // each represented by its smallest point. A grid of 1e-7 m cells finds the neighbours.
    constexpr double snap = terrainDrapeSnap;
    std::unordered_map<CellKey, std::vector<std::uint32_t>, CellHash> grid;
    auto cellOf = [&](TerrainVec2 p) {
        return CellKey{std::int64_t(std::floor(p.x / snap)), std::int64_t(std::floor(p.y / snap))};
    };
    std::vector<std::uint32_t> parent(unique.size());
    for (std::uint32_t i = 0; i < parent.size(); ++i)
        parent[i] = i;
    auto root = [&](std::uint32_t i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    std::uint64_t work = 0;
    for (std::uint32_t i = 0; i < unique.size(); ++i) {
        const auto c = cellOf(unique[i]);
        for (std::int64_t dx = -1; dx <= 1; ++dx)
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                const auto found = grid.find({c.x + dx, c.y + dy});
                if (found == grid.end())
                    continue;
                work += found->second.size();
                if (work > terrainDrapePairWork)
                    problems.refuse(limitKind,
                                    "Too many of the lines' points lie within 1e-7 m of each "
                                    "other; simplify the selection.",
                                    unique[i]);
                for (const auto j : found->second)
                    if (planLength(unique[i].x - unique[j].x, unique[i].y - unique[j].y) <= snap) {
                        const auto a = root(i), b = root(j);
                        if (a != b)
                            parent[std::max(a, b)] = std::min(a, b);
                    }
            }
        grid[c].push_back(i);
    }
    // The registry of arranged points: the representatives first, then crossings.
    std::vector<std::uint32_t> index(unique.size());
    for (std::uint32_t i = 0; i < unique.size(); ++i)
        if (root(i) == i) {
            index[i] = std::uint32_t(result.points.size());
            result.points.push_back(unique[i]);
        }
    for (std::uint32_t i = 0; i < unique.size(); ++i)
        index[i] = index[root(i)];
    auto pointOf = [&](const Plan2 &p) {
        const TerrainVec2 q{clean(p[0]), clean(p[1])};
        const auto at = std::lower_bound(unique.begin(), unique.end(), q, lexLess2);
        return index[std::size_t(at - unique.begin())];
    };
    for (std::uint32_t l = 0; l < data.lines.size(); ++l) {
        std::vector<std::uint32_t> ids;
        for (const auto &p : data.lines[l].points) {
            const auto id = pointOf(p);
            if (ids.empty() || ids.back() != id)
                ids.push_back(id);
        }
        bool closed = data.lines[l].closed;
        while (closed && ids.size() > 1 && ids.back() == ids.front())
            ids.pop_back();
        if (closed && ids.size() < 3)
            closed = false;
        if (ids.size() < 2)
            continue;
        result.lines.push_back(std::move(ids));
        result.closed.push_back(closed);
        result.source.push_back(l);
    }
    // Segments, and the points each gains from the others.
    struct Segment {
        std::uint32_t a, b;
    };
    std::vector<Segment> segments;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> segmentOf; // (line, position)
    for (std::uint32_t l = 0; l < result.lines.size(); ++l) {
        const auto &ids = result.lines[l];
        const std::size_t count = result.closed[l] ? ids.size() : ids.size() - 1;
        for (std::size_t i = 0; i < count; ++i) {
            segments.push_back({ids[i], ids[(i + 1) % ids.size()]});
            segmentOf.push_back({l, std::uint32_t(i)});
        }
    }
    std::vector<std::set<std::uint32_t>> gained(segments.size());
    double total = 0;
    for (const auto &s : segments)
        total += planLength(result.points[s.b].x - result.points[s.a].x,
                            result.points[s.b].y - result.points[s.a].y);
    const double cell = std::max(total / double(std::max<std::size_t>(segments.size(), 1)), 1e-6);
    // Each segment is listed in the cells around samples a quarter cell apart.
    std::vector<std::pair<CellKey, std::uint32_t>> listings;
    for (std::uint32_t s = 0; s < segments.size(); ++s) {
        const auto p = result.points[segments[s].a], q = result.points[segments[s].b];
        const double len = planLength(q.x - p.x, q.y - p.y);
        const auto steps = std::size_t(std::ceil(4 * len / cell)) + 1;
        std::vector<CellKey> cells;
        for (std::size_t k = 0; k <= steps; ++k) {
            const double t = double(k) / double(steps);
            const CellKey c{std::int64_t(std::floor((p.x + (q.x - p.x) * t) / cell)),
                            std::int64_t(std::floor((p.y + (q.y - p.y) * t) / cell))};
            for (std::int64_t dx = -1; dx <= 1; ++dx)
                for (std::int64_t dy = -1; dy <= 1; ++dy)
                    cells.push_back({c.x + dx, c.y + dy});
        }
        std::sort(cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        for (const auto &c : cells)
            listings.push_back({c, s});
    }
    std::sort(listings.begin(), listings.end());
    // The pairs are counted before any is tested, so a refusal does not depend on the order.
    std::uint64_t pairs = 0;
    for (std::size_t i = 0; i < listings.size();) {
        std::size_t j = i;
        while (j < listings.size() && listings[j].first == listings[i].first)
            ++j;
        const std::uint64_t n = j - i;
        pairs += n * (n - 1) / 2;
        i = j;
    }
    if (pairs > terrainDrapePairWork)
        problems.refuse(limitKind,
                        "The lines cross or crowd each other too often to drape at once (" +
                            std::to_string(pairs) + " segment pairs to test); drape fewer lines.",
                        result.points.empty() ? TerrainVec2{} : result.points.front());
    // New points (crossings) snap to a point within 1e-7 m that is already there.
    std::unordered_map<CellKey, std::vector<std::uint32_t>, CellHash> registry;
    for (std::uint32_t i = 0; i < result.points.size(); ++i)
        registry[cellOf(result.points[i])].push_back(i);
    auto registerPoint = [&](TerrainVec2 p) {
        const auto c = cellOf(p);
        std::uint32_t best = std::numeric_limits<std::uint32_t>::max();
        double bestDistance = 0;
        for (std::int64_t dx = -1; dx <= 1; ++dx)
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                const auto found = registry.find({c.x + dx, c.y + dy});
                if (found == registry.end())
                    continue;
                for (const auto j : found->second) {
                    const double d = planLength(p.x - result.points[j].x, p.y - result.points[j].y);
                    if (d <= snap && (best == std::numeric_limits<std::uint32_t>::max() ||
                                      d < bestDistance || (d == bestDistance && j < best))) {
                        best = j;
                        bestDistance = d;
                    }
                }
            }
        if (best != std::numeric_limits<std::uint32_t>::max())
            return best;
        const auto id = std::uint32_t(result.points.size());
        result.points.push_back(p);
        registry[c].push_back(id);
        return id;
    };
    std::unordered_map<std::uint64_t, std::uint32_t> crossingOf;
    std::size_t tested = 0;
    for (std::size_t i = 0; i < listings.size();) {
        std::size_t j = i;
        while (j < listings.size() && listings[j].first == listings[i].first)
            ++j;
        for (std::size_t x = i; x < j; ++x)
            for (std::size_t y = x + 1; y < j; ++y) {
                if (++tested % 4096 == 0)
                    checkpoint(progress);
                const auto si = listings[x].second, ti = listings[y].second;
                const auto &s = segments[si], &t = segments[ti];
                const auto P = result.points[s.a], Q = result.points[s.b],
                           C = result.points[t.a], D = result.points[t.b];
                if (std::max(P.x, Q.x) + snap < std::min(C.x, D.x) ||
                    std::max(C.x, D.x) + snap < std::min(P.x, Q.x) ||
                    std::max(P.y, Q.y) + snap < std::min(C.y, D.y) ||
                    std::max(C.y, D.y) + snap < std::min(P.y, Q.y))
                    continue;
                bool touched = false;
                // A point of one within 1e-7 m of the other lies on it.
                for (const auto e : {t.a, t.b})
                    if (e != s.a && e != s.b &&
                        segmentDistance(result.points[e], P, Q) <= snap) {
                        gained[si].insert(e);
                        touched = true;
                    }
                for (const auto e : {s.a, s.b})
                    if (e != t.a && e != t.b &&
                        segmentDistance(result.points[e], C, D) <= snap) {
                        gained[ti].insert(e);
                        touched = true;
                    }
                if (touched)
                    continue;
                const int o1 = orient(P, Q, C), o2 = orient(P, Q, D), o3 = orient(C, D, P),
                          o4 = orient(C, D, Q);
                if (!(o1 * o2 < 0 && o3 * o4 < 0))
                    continue;
                const auto pairKey = (std::uint64_t(std::min(si, ti)) << 32) | std::max(si, ti);
                auto found = crossingOf.find(pairKey);
                if (found == crossingOf.end()) {
                    // Canonical: each segment from its smaller end, the segments in order.
                    TerrainVec2 a0 = P, a1 = Q, b0 = C, b1 = D;
                    if (lexLess(a1, a0))
                        std::swap(a0, a1);
                    if (lexLess(b1, b0))
                        std::swap(b0, b1);
                    if (lexLess(b0, a0) || (b0 == a0 && lexLess(b1, a1))) {
                        std::swap(a0, b0);
                        std::swap(a1, b1);
                    }
                    const double d = (a1.x - a0.x) * (b1.y - b0.y) - (a1.y - a0.y) * (b1.x - b0.x);
                    const double u =
                        ((b0.x - a0.x) * (b1.y - b0.y) - (b0.y - a0.y) * (b1.x - b0.x)) / d;
                    const TerrainVec2 X{clean(a0.x + u * (a1.x - a0.x)),
                                        clean(a0.y + u * (a1.y - a0.y))};
                    if (crossingOf.size() >= terrainDrapeCrossings)
                        problems.refuse(limitKind,
                                        "The lines cross each other more than " +
                                            std::to_string(terrainDrapeCrossings) +
                                            " times; drape fewer lines.",
                                        X);
                    found = crossingOf.emplace(pairKey, registerPoint(X)).first;
                    ++result.intersections;
                }
                const auto X = found->second;
                if (X != s.a && X != s.b)
                    gained[si].insert(X);
                if (X != t.a && X != t.b)
                    gained[ti].insert(X);
            }
        i = j;
    }
    // The lines with their gained points, in order along each segment.
    std::vector<std::vector<std::uint32_t>> lines(result.lines.size());
    for (std::uint32_t s = 0; s < segments.size(); ++s) {
        const auto [l, position] = segmentOf[s];
        auto &out = lines[l];
        const auto P = result.points[segments[s].a], Q = result.points[segments[s].b];
        if (out.empty() || out.back() != segments[s].a)
            out.push_back(segments[s].a);
        std::vector<std::pair<double, std::uint32_t>> along;
        const double dx = Q.x - P.x, dy = Q.y - P.y, len2 = dx * dx + dy * dy;
        for (const auto e : gained[s]) {
            const auto p = result.points[e];
            along.push_back({((p.x - P.x) * dx + (p.y - P.y) * dy) / len2, e});
        }
        std::sort(along.begin(), along.end());
        for (const auto &[t, e] : along)
            if (out.back() != e)
                out.push_back(e);
        if (!result.closed[l] && position + 1 == result.lines[l].size() - 1 &&
            out.back() != segments[s].b)
            out.push_back(segments[s].b);
    }
    for (std::size_t l = 0; l < lines.size(); ++l) {
        auto &ids = lines[l];
        while (result.closed[l] && ids.size() > 1 && ids.back() == ids.front())
            ids.pop_back();
        result.lines[l] = std::move(ids);
    }
    return result;
}

// 2. Locations in the mesh.
struct Loc {
    enum class Kind : std::uint8_t { Outside, Vertex, Edge, Face };
    Kind kind{Kind::Outside};
    Vec3 p;                 // the position (Vertex: the mesh vertex; Edge, Face: surface Z)
    Vec3 a, b;              // Edge: its ends, `a` lexicographically smaller
    double s{};             // Edge: the parameter from `a`
    TerrainTriangleRef tri; // Face
};
bool sameEdge(const Loc &l, const Vec3 &a, const Vec3 &b) {
    return l.kind == Loc::Kind::Edge && ((l.a == a && l.b == b) || (l.a == b && l.b == a));
}
// Whether a location lies on the (closed) mesh edge a–b.
bool onEdge(const Loc &l, const Vec3 &a, const Vec3 &b) {
    if (l.kind == Loc::Kind::Vertex)
        return keyOf(l.p) == keyOf(a) || keyOf(l.p) == keyOf(b);
    return sameEdge(l, a, b);
}
Vec3 onEdgeAt(const Vec3 &a, const Vec3 &b, double s, TerrainVec2 xy) {
    // The edge's elevation at the parameter, from its smaller end; an edge at one elevation (a
    // contour edge) gives exactly that elevation.
    const double z = a.z == b.z ? a.z : a.z + s * (b.z - a.z);
    return {clean(xy.x), clean(xy.y), clean(z)};
}
double planeZ(const std::array<Vec3, 3> &corners, TerrainVec2 p) {
    auto c = corners;
    std::sort(c.begin(), c.end(), [](const Vec3 &u, const Vec3 &w) { return lexLess(u, w); });
    const auto &A = c[0], &B = c[1], &C = c[2];
    if (A.z == B.z && B.z == C.z)
        return A.z;
    const double det = (B.x - A.x) * (C.y - A.y) - (C.x - A.x) * (B.y - A.y);
    const double u = ((p.x - A.x) * (C.y - A.y) - (C.x - A.x) * (p.y - A.y)) / det;
    const double v = ((B.x - A.x) * (p.y - A.y) - (p.x - A.x) * (B.y - A.y)) / det;
    return A.z + u * (B.z - A.z) + v * (C.z - A.z);
}

class Splitter {
  public:
    Splitter(const TerrainEditMesh &mesh, Problems &problems, TerrainProgress *progress)
        : mesh_(mesh), view_(mesh), problems_(problems), progress_(progress) {}
    const TerrainMeshView &view() const { return view_; }

    Loc locate(TerrainVec2 p) {
        constexpr double snap = terrainDrapeSnap, edgeSnap = terrainDrapeEdgeSnap;
        Loc loc;
        loc.p = {p.x, p.y, 0};
        std::optional<Vec3> vertex;
        double vertexDistance = 0;
        struct Near {
            Vec3 a, b;
            double d, s;
        };
        std::optional<Near> edge;
        std::vector<TerrainTriangleRef> inside;
        for (const auto *tile : mesh_.tilesIn(p.x - snap, p.y - snap, p.x + snap, p.y + snap, 1e-6)) {
            const auto &content = *tile->content;
            scan(content.triangles.size(), p);
            for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
                const auto &tri = content.triangles[t];
                const std::array<Vec3, 3> c{content.vertices[tri[0]], content.vertices[tri[1]],
                                            content.vertices[tri[2]]};
                if (p.x < std::min({c[0].x, c[1].x, c[2].x}) - snap ||
                    p.x > std::max({c[0].x, c[1].x, c[2].x}) + snap ||
                    p.y < std::min({c[0].y, c[1].y, c[2].y}) - snap ||
                    p.y > std::max({c[0].y, c[1].y, c[2].y}) + snap)
                    continue;
                for (int i = 0; i < 3; ++i) {
                    const auto &v = c[std::size_t(i)];
                    const double d = planLength(v.x - p.x, v.y - p.y);
                    if (d <= snap && (!vertex || d < vertexDistance ||
                                      (d == vertexDistance && lexLess(v, *vertex)))) {
                        vertex = v;
                        vertexDistance = d;
                    }
                    auto a = v, b = c[std::size_t((i + 1) % 3)];
                    if (lexLess(b, a))
                        std::swap(a, b);
                    double s = 0;
                    const double ed = segmentDistance(p, v2(a), v2(b), &s);
                    if (s > 0 && s < 1 && (ed <= edgeSnap || orient(v2(a), v2(b), p) == 0) &&
                        (!edge || ed < edge->d ||
                         (ed == edge->d && (lexLess(a, edge->a) ||
                                            (a == edge->a && lexLess(b, edge->b))))))
                        edge = Near{a, b, ed, s};
                }
                if (terrain_view::inTriangle(v2(c[0]), v2(c[1]), v2(c[2]), p))
                    inside.push_back({content.key, t});
            }
        }
        if (vertex) {
            loc.kind = Loc::Kind::Vertex;
            loc.p = *vertex;
            return loc;
        }
        if (edge && (edge->d <= edgeSnap || !inside.empty())) {
            // The point moves onto the edge (at most 1e-9 m), so its elevation, the edge's, is
            // the surface's there.
            const auto &a = edge->a, &b = edge->b;
            const double s = edge->s;
            return edgePoint(a, b, s, edge->d == 0 ? p : TerrainVec2{a.x + s * (b.x - a.x), a.y + s * (b.y - a.y)});
        }
        if (!inside.empty()) {
            std::sort(inside.begin(), inside.end());
            loc.kind = Loc::Kind::Face;
            loc.tri = inside.front();
            loc.p = {clean(p.x), clean(p.y), clean(planeZ(view_.corners(loc.tri), p))};
        }
        return loc;
    }
    // The locations along one segment, its ends included.
    std::vector<Loc> split(const Loc &from, const Loc &to) {
        const auto P = v2(from.p), Q = v2(to.p);
        if (keyOf(P) == keyOf(Q))
            return {from};
        const auto tris = view_.touching(P, Q);
        touched_ += tris.size();
        if (touched_ > terrainDrapeTouched)
            problems_.refuse(limitKind,
                             "The lines touch too many terrain triangles (over " +
                                 std::to_string(terrainDrapeTouched) +
                                 " in all); drape fewer or shorter lines.",
                             P);
        const bool flip = lexLess(Q, P);
        const auto Pc = flip ? Q : P, Qc = flip ? P : Q;
        // Mesh vertices within 1e-7 m of the segment: it is bent through them.
        std::map<TerrainPlanKey, Vec3> corners;
        std::map<std::pair<TerrainPlanKey, TerrainPlanKey>, std::pair<Vec3, Vec3>> edges;
        for (const auto &ref : tris) {
            const auto c = view_.corners(ref);
            for (int i = 0; i < 3; ++i) {
                corners.emplace(keyOf(c[std::size_t(i)]), c[std::size_t(i)]);
                auto a = c[std::size_t(i)], b = c[std::size_t((i + 1) % 3)];
                if (lexLess(b, a))
                    std::swap(a, b);
                edges.emplace(std::pair{keyOf(a), keyOf(b)}, std::pair{a, b});
            }
        }
        std::vector<std::pair<double, Vec3>> snapped;
        for (const auto &[key, v] : corners) {
            if ((from.kind == Loc::Kind::Vertex && keyOf(from.p) == key) ||
                (to.kind == Loc::Kind::Vertex && keyOf(to.p) == key))
                continue;
            double t = 0;
            if (segmentDistance(v2(v), Pc, Qc, &t) <= terrainDrapeSnap) {
                snapped.push_back({t, v});
                ++snappedCount_;
            }
        }
        std::sort(snapped.begin(), snapped.end(), [](const auto &l, const auto &r) {
            return l.first < r.first || (l.first == r.first && lexLess(l.second, r.second));
        });
        if (flip)
            std::reverse(snapped.begin(), snapped.end());
        std::vector<Loc> stops{from};
        for (const auto &[t, v] : snapped) {
            Loc loc;
            loc.kind = Loc::Kind::Vertex;
            loc.p = v;
            stops.push_back(loc);
        }
        stops.push_back(to);
        // The edges by the range of their projection on the segment, so each piece between two
        // stops tests only the edges beside it (a line through many grid vertices has many).
        struct Span {
            double lo, hi;
            const std::pair<Vec3, Vec3> *edge;
        };
        const double sx = Qc.x - Pc.x, sy = Qc.y - Pc.y, len2 = sx * sx + sy * sy;
        auto along = [&](TerrainVec2 v) { return ((v.x - Pc.x) * sx + (v.y - Pc.y) * sy) / len2; };
        std::vector<Span> spans;
        spans.reserve(edges.size());
        double widest = 0;
        for (const auto &[key, edge] : edges) {
            const double t0 = along(v2(edge.first)), t1 = along(v2(edge.second));
            spans.push_back({std::min(t0, t1), std::max(t0, t1), &edge});
            widest = std::max(widest, spans.back().hi - spans.back().lo);
        }
        std::sort(spans.begin(), spans.end(), [](const Span &l, const Span &r) {
            return l.lo < r.lo || (l.lo == r.lo && (lexLess(l.edge->first, r.edge->first) ||
                                                    (l.edge->first == r.edge->first &&
                                                     lexLess(l.edge->second, r.edge->second))));
        });
        constexpr double slack = 1e-9;
        std::vector<Loc> result{from};
        for (std::size_t k = 0; k + 1 < stops.size(); ++k) {
            const auto &A = stops[k], &B = stops[k + 1];
            const auto a = v2(A.p), b = v2(B.p);
            if (keyOf(a) == keyOf(b))
                continue;
            const bool sflip = lexLess(b, a);
            const auto Ac = sflip ? b : a, Bc = sflip ? a : b;
            auto attached = [&](const Loc &l, const Vec3 &u, const Vec3 &w) {
                if (l.kind == Loc::Kind::Vertex)
                    return keyOf(l.p) == keyOf(u) || keyOf(l.p) == keyOf(w);
                return sameEdge(l, u, w);
            };
            const double ta = along(a), tb = along(b);
            const double lo = std::min(ta, tb) - slack, hi = std::max(ta, tb) + slack;
            auto first = std::lower_bound(spans.begin(), spans.end(), lo - widest,
                                          [](const Span &span, double t) { return span.lo < t; });
            std::vector<std::pair<double, Loc>> crossings;
            for (auto span = first; span != spans.end() && span->lo <= hi; ++span) {
                scan(1, a);
                if (span->hi < lo)
                    continue;
                const auto &[u, w] = *span->edge;
                if (attached(A, u, w) || attached(B, u, w))
                    continue;
                const int o1 = orient(Ac, Bc, v2(u)), o2 = orient(Ac, Bc, v2(w)),
                          o3 = orient(v2(u), v2(w), Ac), o4 = orient(v2(u), v2(w), Bc);
                if (!(o1 * o2 < 0 && o3 * o4 < 0))
                    continue;
                const double d = (w.x - u.x) * (Bc.y - Ac.y) - (w.y - u.y) * (Bc.x - Ac.x);
                const double s =
                    ((Ac.x - u.x) * (Bc.y - Ac.y) - (Ac.y - u.y) * (Bc.x - Ac.x)) / d;
                const double t = ((Ac.x - u.x) * (w.y - u.y) - (Ac.y - u.y) * (w.x - u.x)) / d;
                const TerrainVec2 xy{u.x + s * (w.x - u.x), u.y + s * (w.y - u.y)};
                crossings.push_back({t, edgePoint(u, w, s, xy, true)});
            }
            std::sort(crossings.begin(), crossings.end(), [](const auto &l, const auto &r) {
                return l.first < r.first || (l.first == r.first && lexLess(l.second.p, r.second.p));
            });
            if (sflip)
                std::reverse(crossings.begin(), crossings.end());
            for (auto &[t, loc] : crossings)
                if (keyOf(loc.p) != keyOf(result.back().p))
                    result.push_back(loc);
            if (keyOf(B.p) != keyOf(result.back().p))
                result.push_back(B);
        }
        return result;
    }
    // The triangles a location lies in (closed), in reference order.
    std::vector<TerrainTriangleRef> trianglesOf(const Loc &l) const {
        switch (l.kind) {
        case Loc::Kind::Vertex:
            return view_.at(l.p);
        case Loc::Kind::Edge:
            return view_.edge(l.a, l.b);
        case Loc::Kind::Face:
            return {l.tri};
        default:
            return {};
        }
    }
    std::size_t snappedCount() const { return snappedCount_; }
    std::size_t crossingCount() const { return crossingCount_; }
    const std::map<std::pair<TerrainPlanKey, TerrainPlanKey>, std::vector<Loc>> &edgeCrossings() const {
        return byEdge_;
    }

  private:
    void scan(std::size_t work, TerrainVec2 at) {
        scanned_ += work;
        if (scanned_ > terrainDrapeScanWork)
            problems_.refuse(limitKind,
                             "Splitting the lines on the terrain would take too much work (they "
                             "scan over " +
                                 std::to_string(terrainDrapeScanWork) +
                                 " triangles and edges); drape fewer or shorter lines.",
                             at);
        if (++calls_ % 4096 == 0)
            checkpoint(progress_);
    }
    // A point on the mesh edge u–w (u lexicographically smaller) at parameter s: points of one
    // edge within 1e-7 m of each other are one point (the first found), so two lines that cross
    // an edge almost together, or a line's point on an edge and another line's crossing, share it.
    Loc edgePoint(const Vec3 &u, const Vec3 &w, double s, TerrainVec2 xy, bool crossing = false) {
        auto &list = byEdge_[{keyOf(u), keyOf(w)}];
        for (const auto &existing : list)
            if (planLength(existing.p.x - xy.x, existing.p.y - xy.y) <= terrainDrapeSnap)
                return existing;
        Loc loc;
        loc.kind = Loc::Kind::Edge;
        loc.a = u;
        loc.b = w;
        loc.s = s;
        loc.p = onEdgeAt(u, w, s, xy);
        list.push_back(loc);
        crossingCount_ += crossing;
        return loc;
    }
    const TerrainEditMesh &mesh_;
    TerrainMeshView view_;
    Problems &problems_;
    TerrainProgress *progress_;
    std::size_t touched_{}, snappedCount_{}, crossingCount_{}, calls_{};
    std::uint64_t scanned_{};
    std::map<std::pair<TerrainPlanKey, TerrainPlanKey>, std::vector<Loc>> byEdge_;
};
// A drape's split segments by plan key, and how much its effect grows the current sources: its
// runs and its split points once per place a source passes a split segment. The plan and the
// `sources` hook count alike, so a command is refused exactly when its stored effect would be.
struct DrapeSplitIndex {
    using SegmentKey = std::pair<TerrainPlanKey, TerrainPlanKey>;
    std::map<SegmentKey, const TerrainDrapeSplit *> bySegment;
    Box box = emptyBox();
    const TerrainDrapeData &data;
    explicit DrapeSplitIndex(const TerrainDrapeData &drape) : data(drape) {
        for (const auto &split : data.splits) {
            bySegment.emplace(SegmentKey{TerrainPlanKey::of(split.a[0], split.a[1]),
                                         TerrainPlanKey::of(split.b[0], split.b[1])},
                              &split);
            extend(box, split.a[0], split.a[1]);
            extend(box, split.b[0], split.b[1]);
        }
    }
    // The split segment a source's segment is (reversed when it runs from b to a); segments
    // outside the splits' box are skipped before any lookup.
    const TerrainDrapeSplit *splitOf(const Vec3 &a, const Vec3 &b, bool &reverse) const {
        if (bySegment.empty() || std::max(a.x, b.x) < box[0] || std::min(a.x, b.x) > box[2] ||
            std::max(a.y, b.y) < box[1] || std::min(a.y, b.y) > box[3])
            return nullptr;
        const auto ka = keyOf(a), kb = keyOf(b);
        if (const auto found = bySegment.find({ka, kb}); found != bySegment.end()) {
            reverse = false;
            return found->second;
        }
        if (const auto back = bySegment.find({kb, ka}); back != bySegment.end()) {
            reverse = true;
            return back->second;
        }
        return nullptr;
    }
    std::size_t growth(const TerrainSourceState &state,
                       std::vector<const TerrainSource *> *touched = nullptr) const {
        std::size_t total = 0;
        for (const auto &run : data.runs)
            total += run.size();
        if (bySegment.empty())
            return total;
        for (const auto *source : state.sources()) {
            if (source->role != TerrainSourceRole::Contour &&
                source->role != TerrainSourceRole::Breakline)
                continue;
            const auto &p = source->points;
            bool any = false;
            for (std::size_t i = 0; i + 1 < p.size(); ++i) {
                bool reverse = false;
                if (const auto *split = splitOf(p[i], p[i + 1], reverse)) {
                    total += split->points.size();
                    any = true;
                }
            }
            if (any && touched)
                touched->push_back(source);
        }
        return total;
    }
};
} // namespace

TerrainDrapePlan planTerrainDrape(const TerrainEditContext &context, const TerrainEdit &edit,
                                  bool overlay) {
    const auto &data = dataOf(edit);
    const auto &state = context.sources;
    TerrainProgress *progress = context.progress;
    Problems problems(overlay ? "Drape overlay" : "Drape on terrain");
    TerrainDrapePlan plan;
    checkpoint(progress);
    // ---- 1. The lines, meeting only at shared points ----
    const auto arranged = arrange(data, problems, progress);
    plan.intersections = arranged.intersections;
    if (arranged.lines.empty())
        problems.refuse("zero-length-segments",
                        "The lines have no length once points within 1e-7 m of each other are "
                        "merged.",
                        arranged.points.empty() ? TerrainVec2{} : arranged.points.front());
    // ---- 2. Split against the mesh ----
    Splitter splitter(context.mesh, problems, progress);
    const auto &view = splitter.view();
    std::vector<std::optional<Loc>> located(arranged.points.size());
    auto locOf = [&](std::uint32_t id) -> const Loc & {
        if (!located[id])
            located[id] = splitter.locate(arranged.points[id]);
        return *located[id];
    };
    struct Interior {
        TerrainTriangleRef tri;
        Vec3 a, b;
    };
    std::vector<Interior> interior;
    std::vector<std::vector<Loc>> runs;
    std::vector<std::uint32_t> runLines;
    std::size_t points = 0;
    bool inconsistent = false;
    for (std::uint32_t l = 0; l < arranged.lines.size(); ++l) {
        checkpoint(progress);
        const auto &ids = arranged.lines[l];
        const bool closed = arranged.closed[l];
        // The locations along the whole line (a closed line ends where it started).
        std::vector<Loc> along;
        const std::size_t count = closed ? ids.size() : ids.size() - 1;
        for (std::size_t i = 0; i < count; ++i) {
            const auto part = splitter.split(locOf(ids[i]), locOf(ids[(i + 1) % ids.size()]));
            for (const auto &loc : part)
                if (along.empty() || keyOf(along.back().p) != keyOf(loc.p))
                    along.push_back(loc);
            points += part.size();
            if (points > 4 * terrainDrapeResolvedPoints)
                problems.refuse(limitKind,
                                "The draped lines would gain more than " +
                                    std::to_string(terrainDrapeResolvedPoints) +
                                    " points; drape fewer or shorter lines.",
                                v2(part.front().p));
        }
        if (along.size() < 2)
            continue;
        // Pieces: inside one triangle, along a mesh edge, or a gap outside the outline.
        const std::size_t pieceCount = along.size() - 1;
        std::vector<char> gap(pieceCount, 0);
        for (std::size_t i = 0; i < pieceCount; ++i) {
            const auto &A = along[i], &B = along[i + 1];
            const auto ta = splitter.trianglesOf(A), tb = splitter.trianglesOf(B);
            std::vector<TerrainTriangleRef> common;
            std::set_intersection(ta.begin(), ta.end(), tb.begin(), tb.end(),
                                  std::back_inserter(common));
            const TerrainVec2 middle{A.p.x + (B.p.x - A.p.x) / 2, A.p.y + (B.p.y - A.p.y) / 2};
            if (common.empty()) {
                if (!view.containing(middle).empty()) {
                    problems.add(splitKind, splitMessage, middle);
                    inconsistent = true;
                }
                gap[i] = 1;
                continue;
            }
            // Along a mesh edge: both ends on one edge of a common triangle.
            bool alongEdge = false;
            for (const auto &ref : common) {
                const auto c = view.corners(ref);
                for (int e = 0; e < 3 && !alongEdge; ++e)
                    alongEdge = onEdge(A, c[std::size_t(e)], c[std::size_t((e + 1) % 3)]) &&
                                onEdge(B, c[std::size_t(e)], c[std::size_t((e + 1) % 3)]);
            }
            if (alongEdge)
                continue;
            if (common.size() != 1) {
                problems.add(splitKind, splitMessage, middle);
                inconsistent = true;
                continue;
            }
            interior.push_back({common.front(), A.p, B.p});
        }
        // Runs between gaps (a closed line without a gap is one closed run).
        std::size_t start = 0;
        bool wrap = false;
        if (closed) {
            std::size_t firstGap = pieceCount;
            for (std::size_t i = 0; i < pieceCount; ++i)
                if (gap[i]) {
                    firstGap = i;
                    break;
                }
            if (firstGap == pieceCount) {
                std::vector<Loc> run = along; // repeats its first point (a closed line ends there)
                if (keyOf(run.back().p) != keyOf(run.front().p))
                    run.push_back(run.front());
                runs.push_back(std::move(run));
                runLines.push_back(arranged.source[l]);
                continue;
            }
            start = firstGap + 1;
            wrap = true;
        }
        std::vector<Loc> run;
        bool inGap = false;
        for (std::size_t n = 0; n < pieceCount; ++n) {
            const std::size_t i = wrap ? (start + n) % pieceCount : n;
            if (gap[i]) {
                if (!inGap) {
                    ++plan.clipped;
                    if (plan.clipped == 1)
                        plan.clippedAt = {along[i].p.x + (along[i + 1].p.x - along[i].p.x) / 2,
                                          along[i].p.y + (along[i + 1].p.y - along[i].p.y) / 2};
                }
                inGap = true;
                if (run.size() >= 2) {
                    runs.push_back(std::move(run));
                    runLines.push_back(arranged.source[l]);
                }
                run.clear();
                continue;
            }
            inGap = false;
            if (run.empty())
                run.push_back(along[i]);
            run.push_back(along[i + 1]);
        }
        if (run.size() >= 2) {
            runs.push_back(std::move(run));
            runLines.push_back(arranged.source[l]);
        }
    }
    if (inconsistent)
        problems.refuse();
    if (runs.empty())
        problems.refuse("outside-outline",
                        "No part of the lines lies on the terrain; drapes stay inside the "
                        "terrain's outline and outside its holes.",
                        plan.clipped ? plan.clippedAt
                                     : (arranged.points.empty() ? TerrainVec2{}
                                                                : arranged.points.front()));
    if (runs.size() > terrainDrapeRuns)
        problems.refuse(limitKind,
                        "The drape would make " + std::to_string(runs.size()) +
                            " lines (at most 4,096); drape fewer lines.",
                        v2(runs[terrainDrapeRuns].front().p));
    // ---- 3. New vertices, by edge and by triangle ----
    using EdgeKey = std::pair<TerrainPlanKey, TerrainPlanKey>;
    std::map<EdgeKey, std::vector<Loc>> edgePoints;
    std::map<TerrainTriangleRef, std::vector<Loc>> facePoints;
    {
        std::set<TerrainPlanKey> seen;
        for (const auto &run : runs)
            for (const auto &loc : run) {
                if (loc.kind != Loc::Kind::Edge && loc.kind != Loc::Kind::Face)
                    continue;
                if (!seen.insert(keyOf(loc.p)).second)
                    continue;
                if (loc.kind == Loc::Kind::Edge)
                    edgePoints[{keyOf(loc.a), keyOf(loc.b)}].push_back(loc);
                else
                    facePoints[loc.tri].push_back(loc);
            }
        for (auto &[key, list] : edgePoints)
            std::sort(list.begin(), list.end(), [](const Loc &l, const Loc &r) {
                return l.s < r.s || (l.s == r.s && lexLess(l.p, r.p));
            });
        plan.inserted = seen.size();
    }
    // A run along a mesh edge passes every new vertex on it (so each run segment is a mesh edge,
    // check C1).
    for (auto &run : runs) {
        std::vector<Loc> full{run.front()};
        for (std::size_t i = 0; i + 1 < run.size(); ++i) {
            const auto &A = run[i], &B = run[i + 1];
            const Vec3 *ea = nullptr, *eb = nullptr;
            if (A.kind == Loc::Kind::Edge && onEdge(B, A.a, A.b)) {
                ea = &A.a;
                eb = &A.b;
            } else if (B.kind == Loc::Kind::Edge && onEdge(A, B.a, B.b)) {
                ea = &B.a;
                eb = &B.b;
            } else if (A.kind == Loc::Kind::Vertex && B.kind == Loc::Kind::Vertex) {
                ea = &A.p;
                eb = &B.p;
                if (lexLess(*eb, *ea))
                    std::swap(ea, eb);
            }
            if (ea) {
                const auto found = edgePoints.find({keyOf(*ea), keyOf(*eb)});
                if (found != edgePoints.end()) {
                    auto param = [&](const Loc &l) {
                        if (l.kind == Loc::Kind::Edge)
                            return l.s;
                        return keyOf(l.p) == keyOf(*ea) ? 0.0 : 1.0;
                    };
                    const double s0 = param(A), s1 = param(B);
                    std::vector<const Loc *> between;
                    for (const auto &e : found->second)
                        if (e.s > std::min(s0, s1) && e.s < std::max(s0, s1) &&
                            keyOf(e.p) != keyOf(A.p) && keyOf(e.p) != keyOf(B.p))
                            between.push_back(&e);
                    if (s1 < s0)
                        std::reverse(between.begin(), between.end());
                    for (const auto *e : between)
                        full.push_back(*e);
                }
            }
            full.push_back(B);
        }
        run = std::move(full);
    }
    for (const auto &run : runs)
        plan.points += run.size();
    if (plan.points > terrainDrapeResolvedPoints)
        problems.refuse(limitKind,
                        "The draped lines would have more than " +
                            std::to_string(terrainDrapeResolvedPoints) +
                            " points; drape fewer or shorter lines.",
                        v2(runs.front().front().p));
    plan.crossings = splitter.crossingCount();
    plan.snapped = splitter.snappedCount();
    // ---- 4. Pads (decision 6): a run may not meet a pad's footprint or outline ----
    if (!overlay) {
        const TerrainPadIndex pads(state.protectedRegions);
        if (!pads.empty())
            for (const auto &run : runs)
                for (std::size_t i = 0; i < run.size(); ++i) {
                    if (const auto pad = pads.covers(run[i].p.x, run[i].p.y)) {
                        problems.add("inside-pad",
                                     "A building pad protects this area; drape around it or "
                                     "remove its stamp first.",
                                     v2(run[i].p), 0, pad);
                        continue;
                    }
                    if (i + 1 < run.size())
                        if (const auto pad = pads.meetsSegment(v2(run[i].p), v2(run[i + 1].p)))
                            problems.add("inside-pad",
                                         "A building pad protects this area; drape around it or "
                                         "remove its stamp first.",
                                         {(run[i].p.x + run[i + 1].p.x) / 2,
                                          (run[i].p.y + run[i + 1].p.y) / 2},
                                         0, pad);
                }
        problems.check();
        if (!terrainSplitSourceId(edit.id, std::uint32_t(runs.size() - 1)))
            problems.refuse(limitKind,
                            "The terrain has used every source id for draped lines; use Reset "
                            "edits, or Explode terrain to keep the surface as ordinary geometry.",
                            v2(runs.front().front().p));
    }
    // ---- 5. Split the triangles, each within itself ----
    std::set<TerrainTriangleRef> affected;
    for (const auto &[key, list] : edgePoints)
        for (const auto &ref : view.edge(list.front().a, list.front().b))
            affected.insert(ref);
    for (const auto &[ref, list] : facePoints)
        affected.insert(ref);
    if (affected.size() > terrainDrapeTriangles)
        problems.refuse(limitKind,
                        "The drape would split more than " +
                            std::to_string(terrainDrapeTriangles) +
                            " terrain triangles; split it into smaller drapes.",
                        v2(runs.front().front().p));
    std::map<TerrainTriangleRef, std::vector<std::pair<Vec3, Vec3>>> pieces;
    for (const auto &piece : interior) {
        if (!affected.contains(piece.tri))
            problems.add(splitKind, splitMessage,
                         {(piece.a.x + piece.b.x) / 2, (piece.a.y + piece.b.y) / 2});
        pieces[piece.tri].push_back({piece.a, piece.b});
    }
    problems.check();
    TerrainTriangulationOptions options;
    options.flatMode = TerrainFlatMode::ReportOnly;
    std::size_t done = 0;
    for (const auto &ref : affected) {
        if (++done % 64 == 0)
            checkpoint(progress);
        const auto c = view.corners(ref);
        TerrainCavityInput input;
        std::vector<Vec3> positions;
        std::map<TerrainPlanKey, std::uint32_t> index;
        auto add = [&](const Vec3 &p) {
            const auto [it, fresh] = index.try_emplace(keyOf(p), std::uint32_t(positions.size()));
            if (fresh) {
                positions.push_back(p);
                input.vertices.push_back({{p.x, p.y, p.z}, false, 0});
            }
            return it->second;
        };
        std::vector<std::uint32_t> ring;
        for (int i = 0; i < 3; ++i) {
            const auto &u = c[std::size_t(i)], &w = c[std::size_t((i + 1) % 3)];
            ring.push_back(add(u));
            const bool forward = lexLess(u, w);
            const auto found = edgePoints.find(forward ? EdgeKey{keyOf(u), keyOf(w)}
                                                       : EdgeKey{keyOf(w), keyOf(u)});
            if (found == edgePoints.end())
                continue;
            std::vector<const Loc *> list;
            for (const auto &loc : found->second)
                list.push_back(&loc);
            if (!forward)
                std::reverse(list.begin(), list.end());
            for (const auto *loc : list)
                ring.push_back(add(loc->p));
        }
        for (std::size_t i = 0; i < ring.size(); ++i)
            input.boundary.push_back({ring[i], ring[(i + 1) % ring.size()]});
        if (const auto found = facePoints.find(ref); found != facePoints.end())
            for (const auto &loc : found->second)
                add(loc.p);
        if (const auto found = pieces.find(ref); found != pieces.end())
            for (const auto &[a, b] : found->second) {
                const auto ia = index.find(keyOf(a)), ib = index.find(keyOf(b));
                if (ia == index.end() || ib == index.end() || ia->second == ib->second) {
                    problems.add(splitKind, splitMessage, {(a.x + b.x) / 2, (a.y + b.y) / 2});
                    continue;
                }
                input.constraints.push_back({ia->second, ib->second});
            }
        std::sort(input.constraints.begin(), input.constraints.end());
        input.constraints.erase(std::unique(input.constraints.begin(), input.constraints.end()),
                                input.constraints.end());
        const auto result = retriangulateTerrainCavity(input, options);
        if (!result.ok) {
            for (const auto &problem : result.report.refusals)
                problems.merge(problem);
            if (!problems.any())
                problems.add(splitKind, splitMessage,
                             {(c[0].x + c[1].x + c[2].x) / 3, (c[0].y + c[1].y + c[2].y) / 3});
            continue;
        }
        TerrainDrapePiece piece{ref, std::uint32_t(plan.change.added.size()), 0};
        for (const auto &t : result.triangles) {
            const std::array<Vec3, 3> tri{positions[t[0]], positions[t[1]], positions[t[2]]};
            if (length(cross(tri[1] - tri[0], tri[2] - tri[0])) < 1e-12)
                problems.add("degenerate-triangle",
                             "The drape would make a sliver below the modelling tolerance (area "
                             "under 5e-13 m²); move the line slightly.",
                             {(tri[0].x + tri[1].x + tri[2].x) / 3,
                              (tri[0].y + tri[1].y + tri[2].y) / 3});
            plan.change.added.push_back(tri);
            ++piece.count;
        }
        plan.pieces.push_back(piece);
    }
    problems.check();
    plan.change.removed.assign(affected.begin(), affected.end());
    // ---- 6. The resolved geometry ----
    auto resolved = std::make_shared<TerrainDrapeData>();
    resolved->lines = data.lines;
    resolved->paint = data.paint;
    for (const auto &run : runs) {
        std::vector<Vec3> points;
        points.reserve(run.size());
        for (const auto &loc : run)
            points.push_back(loc.p);
        resolved->runs.push_back(std::move(points));
    }
    std::size_t splitPoints = 0;
    for (const auto &[key, list] : edgePoints) {
        const auto &a = list.front().a, &b = list.front().b;
        if (!state.segmentUses(keyOf(a), keyOf(b)))
            continue;
        TerrainDrapeSplit split;
        split.a = {a.x, a.y};
        split.b = {b.x, b.y};
        for (const auto &loc : list)
            split.points.push_back(loc.p);
        splitPoints += split.points.size();
        resolved->splits.push_back(std::move(split));
    }
    if (splitPoints > terrainDrapeSplitPoints)
        problems.refuse(limitKind,
                        "The drape would split existing contours and breaklines more than " +
                            std::to_string(terrainDrapeSplitPoints) + " times.",
                        v2(runs.front().front().p));
    plan.splitSegments = resolved->splits.size();
    // The current sources' growth, counted as the `sources` hook counts it.
    if (!overlay &&
        state.vertexCount() + DrapeSplitIndex(*resolved).growth(state) > terrainDrapeSourceVertices)
        problems.refuse(limitKind,
                        "The drape would grow the terrain's current sources past " +
                            std::to_string(terrainDrapeSourceVertices) +
                            " vertices; drape fewer lines, or use Reset edits.",
                        v2(runs.front().front().p));
    plan.runLines = std::move(runLines);
    plan.resolved = std::move(resolved);
    return plan;
}

// ---- Overlay ------------------------------------------------------------------------------------
// Even-odd containment over a region's loops (closed), each loop banded as R096.b's regions.
class TerrainPaintLoop {
  public:
    explicit TerrainPaintLoop(std::vector<Plan2> points) : points_(std::move(points)) {
        box_ = emptyBox();
        for (const auto &p : points_)
            extend(box_, p[0], p[1]);
        bands_ = terrainSculptRegionBands(points_);
        const std::size_t n = points_.size();
        if (n < 3)
            return;
        std::vector<std::uint32_t> counts(bands_.count + 1, 0);
        auto range = [&](std::size_t i) {
            const auto &a = points_[i], &b = points_[(i + 1) % n];
            return std::pair{terrainSculptBandOf(bands_, std::min(a[1], b[1])),
                             terrainSculptBandOf(bands_, std::max(a[1], b[1]))};
        };
        for (std::size_t i = 0; i < n; ++i) {
            const auto [lo, hi] = range(i);
            for (auto band = lo; band <= hi; ++band)
                ++counts[band + 1];
        }
        for (std::size_t band = 0; band < bands_.count; ++band)
            counts[band + 1] += counts[band];
        offsets_ = counts;
        edges_.resize(offsets_.back());
        auto fill = offsets_;
        for (std::size_t i = 0; i < n; ++i) {
            const auto [lo, hi] = range(i);
            for (auto band = lo; band <= hi; ++band)
                edges_[fill[band]++] = std::uint32_t(i);
        }
    }
    const Box &box() const { return box_; }
    // Whether a rightward ray from (x, y) crosses the loop an odd number of times (half-open in Y,
    // exact side test).
    bool odd(double x, double y) const {
        if (points_.size() < 3 || y < box_[1] || y > box_[3] || x > box_[2])
            return false;
        const auto band = terrainSculptBandOf(bands_, y);
        bool parity = false;
        const std::size_t n = points_.size();
        for (auto k = offsets_[band]; k < offsets_[band + 1]; ++k) {
            const auto &a = points_[edges_[k]], &b = points_[(edges_[k] + 1) % n];
            if ((a[1] > y) == (b[1] > y))
                continue;
            const int o = orient(v2(a), v2(b), {x, y});
            if (b[1] > a[1] ? o > 0 : o < 0)
                parity = !parity;
        }
        return parity;
    }

  private:
    std::vector<Plan2> points_;
    Box box_{};
    TerrainSculptBands bands_;
    std::vector<std::uint32_t> offsets_, edges_;
};
namespace {
struct PaintShape {
    std::vector<std::shared_ptr<const TerrainPaintLoop>> loops;
    Box box = emptyBox();
    explicit PaintShape(const std::vector<std::vector<Plan2>> &list) {
        for (const auto &loop : list) {
            loops.push_back(std::make_shared<const TerrainPaintLoop>(loop));
            const auto &b = loops.back()->box();
            extend(box, b[0], b[1]);
            extend(box, b[2], b[3]);
        }
    }
    bool contains(double x, double y) const {
        if (x < box[0] || x > box[2] || y < box[1] || y > box[3])
            return false;
        bool parity = false;
        for (const auto &loop : loops)
            parity = parity != loop->odd(x, y);
        return parity;
    }
};
struct KeyLess {
    bool operator()(const TerrainPositionKey &a, const TerrainPositionKey &b) const {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    }
};
double signedArea2(const std::vector<Vec3> &loop) {
    double area = 0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto &a = loop[i], &b = loop[(i + 1) % loop.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area;
}
} // namespace

TerrainDrapeOverlay terrainDrapeOverlay(const TerrainEditMesh &mesh, const TerrainDrapePlan &plan,
                                        const std::vector<bool> &edgeLines,
                                        const std::vector<std::vector<std::vector<Plan2>>> &faces,
                                        double lift, std::size_t faceLimit,
                                        TerrainProgress *progress) {
    TerrainDrapeOverlay overlay;
    Problems problems("Drape overlay");
    auto lifted = [&](Vec3 p) {
        p.z += lift;
        return p;
    };
    // Edges: the runs of edge lines, each segment once.
    auto edgeKey = [](const Vec3 &a, const Vec3 &b) {
        auto ka = TerrainPositionKey::of(a), kb = TerrainPositionKey::of(b);
        auto tie = [](const TerrainPositionKey &k) { return std::tie(k.x, k.y, k.z); };
        if (tie(kb) < tie(ka))
            std::swap(ka, kb);
        return std::array<std::uint64_t, 6>{ka.x, ka.y, ka.z, kb.x, kb.y, kb.z};
    };
    std::set<std::array<std::uint64_t, 6>> wires;
    for (std::size_t r = 0; r < plan.resolved->runs.size(); ++r) {
        const auto line = plan.runLines[r];
        if (line >= edgeLines.size() || !edgeLines[line])
            continue;
        const auto &run = plan.resolved->runs[r];
        for (std::size_t i = 0; i + 1 < run.size(); ++i)
            if (wires.insert(edgeKey(run[i], run[i + 1])).second)
                overlay.edges.push_back({lifted(run[i]), lifted(run[i + 1])});
    }
    // Faces: each (sub)triangle inside a face belongs to the last such face.
    std::map<TerrainTriangleRef, const TerrainDrapePiece *> split;
    for (const auto &piece : plan.pieces)
        split.emplace(piece.triangle, &piece);
    const TerrainMeshView view(mesh);
    using Part = std::pair<TerrainTriangleRef, std::uint32_t>; // sub-triangle (or ~0 for whole)
    std::map<Part, std::size_t> owner;
    std::uint64_t work = 0;
    for (std::size_t f = 0; f < faces.size(); ++f) {
        const PaintShape shape(faces[f]);
        if (!(shape.box[0] <= shape.box[2]))
            continue;
        for (const auto *tile : mesh.tilesIn(shape.box[0], shape.box[1], shape.box[2],
                                             shape.box[3], 0)) {
            const auto &content = *tile->content;
            for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
                if (++work > terrainDrapeOverlayWork)
                    problems.refuse(limitKind,
                                    "The draped faces cover too much of the terrain to overlay "
                                    "at once; drape fewer faces.",
                                    {shape.box[0], shape.box[1]});
                if (work % 4096 == 0)
                    checkpoint(progress);
                const auto &tri = content.triangles[t];
                const std::array<Vec3, 3> c{content.vertices[tri[0]], content.vertices[tri[1]],
                                            content.vertices[tri[2]]};
                if (std::max({c[0].x, c[1].x, c[2].x}) < shape.box[0] ||
                    std::min({c[0].x, c[1].x, c[2].x}) > shape.box[2] ||
                    std::max({c[0].y, c[1].y, c[2].y}) < shape.box[1] ||
                    std::min({c[0].y, c[1].y, c[2].y}) > shape.box[3])
                    continue;
                const TerrainTriangleRef ref{content.key, t};
                auto test = [&](const std::array<Vec3, 3> &s, std::uint32_t part) {
                    if (shape.contains((s[0].x + s[1].x + s[2].x) / 3, (s[0].y + s[1].y + s[2].y) / 3))
                        owner[{ref, part}] = f;
                };
                if (const auto found = split.find(ref); found != split.end()) {
                    // Every piece of a split triangle is a containment test of its own.
                    work += found->second->count;
                    if (work > terrainDrapeOverlayWork)
                        problems.refuse(limitKind,
                                        "The draped faces cover too much of the terrain to "
                                        "overlay at once; drape fewer faces.",
                                        {shape.box[0], shape.box[1]});
                    for (std::uint32_t k = 0; k < found->second->count; ++k)
                        test(plan.change.added[found->second->first + k], k);
                } else
                    test(c, std::numeric_limits<std::uint32_t>::max());
            }
        }
    }
    // One polygon per (terrain triangle, face): the boundary of its sub-triangles.
    std::map<std::pair<TerrainTriangleRef, std::size_t>, std::vector<std::array<Vec3, 3>>> groups;
    for (const auto &[part, f] : owner) {
        const auto &[ref, k] = part;
        groups[{ref, f}].push_back(k == std::numeric_limits<std::uint32_t>::max()
                                       ? view.corners(ref)
                                       : plan.change.added[split.at(ref)->first + k]);
    }
    for (const auto &[key, triangles] : groups) {
        const auto f = key.second;
        auto emitEach = [&] {
            for (const auto &t : triangles)
                overlay.faces.push_back({f, {{lifted(t[0]), lifted(t[1]), lifted(t[2])}}});
        };
        // Directed edges by position; an edge used both ways is inside the polygon.
        std::map<TerrainPositionKey, Vec3, KeyLess> positions;
        const KeyLess less;
        using K = TerrainPositionKey;
        std::vector<std::pair<K, K>> directed;
        for (const auto &t : triangles)
            for (int i = 0; i < 3; ++i) {
                positions.emplace(K::of(t[std::size_t(i)]), t[std::size_t(i)]);
                directed.push_back({K::of(t[std::size_t(i)]), K::of(t[std::size_t((i + 1) % 3)])});
            }
        auto pairLess = [&](const std::pair<K, K> &a, const std::pair<K, K> &b) {
            if (less(a.first, b.first))
                return true;
            if (less(b.first, a.first))
                return false;
            return less(a.second, b.second);
        };
        std::sort(directed.begin(), directed.end(), pairLess);
        std::vector<std::pair<K, K>> boundary;
        for (const auto &e : directed)
            if (!std::binary_search(directed.begin(), directed.end(), std::pair{e.second, e.first},
                                    pairLess))
                boundary.push_back(e);
        // Each boundary vertex must start exactly one boundary edge (no pinch); otherwise the
        // triangles are emitted one by one.
        bool simple = true;
        for (std::size_t i = 1; i < boundary.size() && simple; ++i)
            simple = !(boundary[i].first == boundary[i - 1].first);
        if (!simple || boundary.empty()) {
            emitEach();
            continue;
        }
        std::vector<char> used(boundary.size(), 0);
        std::vector<std::vector<Vec3>> outers, holes;
        for (std::size_t s = 0; s < boundary.size(); ++s) {
            if (used[s])
                continue;
            std::vector<Vec3> loop;
            std::size_t at = s;
            bool ok = true;
            while (!used[at]) {
                used[at] = 1;
                loop.push_back(positions.at(boundary[at].first));
                const auto next = std::lower_bound(
                    boundary.begin(), boundary.end(), std::pair{boundary[at].second, K{}},
                    [&](const std::pair<K, K> &a, const std::pair<K, K> &b) {
                        return less(a.first, b.first);
                    });
                if (next == boundary.end() || !(next->first == boundary[at].second)) {
                    ok = false;
                    break;
                }
                at = std::size_t(next - boundary.begin());
            }
            if (!ok || at != s || loop.size() < 3) {
                simple = false;
                break;
            }
            (signedArea2(loop) > 0 ? outers : holes).push_back(std::move(loop));
        }
        if (!simple || outers.empty()) {
            emitEach();
            continue;
        }
        std::vector<TerrainDrapeOverlayFace> made;
        std::vector<TerrainPaintLoop> outlines; // each outer loop indexed once (several outers)
        for (auto &outer : outers) {
            TerrainDrapeOverlayFace face{f, {}};
            face.loops.push_back(outer);
            made.push_back(std::move(face));
            if (outers.size() > 1 && !holes.empty()) {
                std::vector<Plan2> outline;
                for (const auto &q : outer)
                    outline.push_back({q.x, q.y});
                outlines.emplace_back(std::move(outline));
            }
        }
        for (auto &hole : holes) {
            std::size_t into = 0;
            for (std::size_t m = 0; m < outlines.size(); ++m)
                if (outlines[m].odd(hole.front().x, hole.front().y)) {
                    into = m;
                    break;
                }
            made[into].loops.push_back(std::move(hole));
        }
        for (auto &face : made) {
            for (auto &loop : face.loops)
                for (auto &p : loop)
                    p = lifted(p);
            overlay.faces.push_back(std::move(face));
        }
        if (overlay.faces.size() > faceLimit)
            problems.refuse(limitKind,
                            "The overlay would make more than " + std::to_string(faceLimit) +
                                " faces; drape fewer faces.",
                            {triangles.front()[0].x, triangles.front()[0].y});
    }
    if (overlay.faces.size() > faceLimit)
        problems.refuse(limitKind,
                        "The overlay would make more than " + std::to_string(faceLimit) +
                            " faces; drape fewer faces.",
                        {});
    return overlay;
}

// ---- The kind -------------------------------------------------------------------------------
namespace {

// The current sources after a resolved drape: the split points inserted into the contours and
// breaklines whose segments they split (and the split pad rays superseded), then the runs as
// breaklines with the entry's block ids.
void applyDrapeSources(TerrainSourceState &state, const TerrainEdit &edit) {
    const auto &data = dataOf(edit);
    if (!data.resolved())
        return;
    const DrapeSplitIndex index(data);
    std::vector<const TerrainSource *> touched;
    const auto growth = index.growth(state, &touched);
    const auto &box = index.box;
    const auto &bySegment = index.bySegment;
    auto splitOf = [&](const Vec3 &a, const Vec3 &b, bool &reverse) {
        return index.splitOf(a, b, reverse);
    };
    if (state.vertexCount() + growth > terrainDrapeSourceVertices)
        throw std::invalid_argument("The drape would grow the current sources past their bound");
    if (!touched.empty()) {
        std::vector<TerrainSource> replaced;
        for (const auto *source : touched) {
            const auto &p = source->points;
            std::vector<Vec3> points{p.front()};
            for (std::size_t i = 0; i + 1 < p.size(); ++i) {
                bool reverse = false;
                if (const auto *split = splitOf(p[i], p[i + 1], reverse)) {
                    if (reverse)
                        points.insert(points.end(), split->points.rbegin(), split->points.rend());
                    else
                        points.insert(points.end(), split->points.begin(), split->points.end());
                }
                points.push_back(p[i + 1]);
            }
            TerrainSource changed = *source;
            changed.points = std::move(points);
            replaced.push_back(std::move(changed));
        }
        for (auto &source : replaced)
            state.replace(std::move(source));
    }
    if (!bySegment.empty())
        // Pad rays the drape split are superseded, as a newer pad's would be.
        state.dropPadRays(box, [&](const Vec3 &a, const Vec3 &b) {
            return bySegment.contains({keyOf(a), keyOf(b)}) ||
                   bySegment.contains({keyOf(b), keyOf(a)});
        });
    for (std::size_t i = 0; i < data.runs.size(); ++i) {
        const auto id = terrainSplitSourceId(edit.id, std::uint32_t(i));
        if (!id || state.find(*id))
            continue;
        TerrainSource line;
        line.id = *id;
        line.role = TerrainSourceRole::Breakline;
        line.reference = "drape " + std::to_string(edit.id);
        line.points = data.runs[i];
        state.markDrapeSource(*id);
        state.add(std::move(line));
    }
}
} // namespace
std::vector<TerrainEditKind> terrainDrapeKinds() {
    TerrainEditKind kind;
    kind.name = terrainDrapeKind;
    kind.applyInContext = [](const TerrainEditContext &context, const TerrainEdit &edit) {
        auto plan = planTerrainDrape(context, edit);
        return TerrainEditOutcome{std::move(plan.change), {}, std::move(plan.resolved)};
    };
    kind.sources = applyDrapeSources;
    kind.bounds = [](const TerrainEdit &edit) { return terrainDrapeBounds(dataOf(edit)); };
    kind.unresolved = [](const TerrainEdit &edit) -> std::shared_ptr<const TerrainEditData> {
        const auto &data = dataOf(edit);
        if (!data.resolved())
            return nullptr;
        auto cleared = std::make_shared<TerrainDrapeData>(data);
        cleared->runs.clear();
        cleared->splits.clear();
        return cleared;
    };
    return {std::move(kind)};
}

// ---- Paint regions --------------------------------------------------------------------------
TerrainPaintRegions::TerrainPaintRegions(const TerrainRecord &record) {
    for (const auto &edit : record.edits) {
        if (edit->kind != terrainDrapeKind)
            continue;
        const auto *data = dynamic_cast<const TerrainDrapeData *>(edit->data.get());
        if (!data || !data->resolved())
            continue;
        for (const auto &paint : data->paint) {
            Region region;
            region.material = paint.material;
            region.box = emptyBox();
            for (const auto &loop : paint.loops) {
                auto index = std::make_shared<const TerrainPaintLoop>(loop);
                const auto &b = index->box();
                extend(region.box, b[0], b[1]);
                extend(region.box, b[2], b[3]);
                region.loops.push_back(std::move(index));
            }
            regions_.push_back(std::move(region));
        }
    }
}
Id TerrainPaintRegions::materialAt(double x, double y, std::size_t first) const {
    for (std::size_t i = regions_.size(); i-- > first;) {
        const auto &region = regions_[i];
        if (x < region.box[0] || x > region.box[2] || y < region.box[1] || y > region.box[3])
            continue;
        bool parity = false;
        for (const auto &loop : region.loops)
            parity = parity != loop->odd(x, y);
        if (parity)
            return region.material;
    }
    return 0;
}
std::vector<std::size_t> TerrainPaintRegions::candidates(const std::array<double, 4> &box,
                                                         std::size_t first) const {
    std::vector<std::size_t> list;
    for (std::size_t i = first; i < regions_.size(); ++i) {
        const auto &b = regions_[i].box;
        if (!(b[2] < box[0] || b[0] > box[2] || b[3] < box[1] || b[1] > box[3]))
            list.push_back(i);
    }
    return list;
}
Id TerrainPaintRegions::materialAt(double x, double y,
                                   const std::vector<std::size_t> &candidates) const {
    for (std::size_t k = candidates.size(); k-- > 0;) {
        const auto &region = regions_[candidates[k]];
        if (x < region.box[0] || x > region.box[2] || y < region.box[1] || y > region.box[3])
            continue;
        bool parity = false;
        for (const auto &loop : region.loops)
            parity = parity != loop->odd(x, y);
        if (parity)
            return region.material;
    }
    return 0;
}
bool TerrainPaintRegions::meets(const std::array<double, 4> &box, std::size_t first) const {
    for (std::size_t i = first; i < regions_.size(); ++i) {
        const auto &b = regions_[i].box;
        if (!(b[2] < box[0] || b[0] > box[2] || b[3] < box[1] || b[1] > box[3]))
            return true;
    }
    return false;
}
namespace {
std::array<double, 2> faceCentroid(const Body &tile, const Face &face) {
    const auto &loop = face.loops.front();
    const auto &a = tile.surface.vertices.at(loop[0]), &b = tile.surface.vertices.at(loop[1]),
               &c = tile.surface.vertices.at(loop[2]);
    return {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3};
}
} // namespace
bool paintTerrainTile(Body &tile, const TerrainPaintRegions &regions, std::size_t first) {
    if (regions.size() <= first || tile.surface.vertices.empty())
        return false;
    // Only the regions whose boxes meet the tile's are tested.
    auto box = emptyBox();
    for (const auto &[id, p] : tile.surface.vertices)
        extend(box, p.x, p.y);
    const auto candidates = regions.candidates(box, first);
    if (candidates.empty())
        return false;
    bool changed = false;
    for (const auto &[id, face] : tile.surface.faces) {
        if (face.loops.empty() || face.loops.front().size() != 3)
            continue;
        const auto c = faceCentroid(tile, face);
        const auto material = regions.materialAt(c[0], c[1], candidates);
        if (!material)
            continue;
        const MaterialSides sides{material, material};
        if (sides == tile.materials) {
            changed = tile.faceMaterials.erase(id) > 0 || changed;
            continue;
        }
        auto &entry = tile.faceMaterials[id];
        changed = changed || entry != sides;
        entry = sides;
    }
    return changed;
}
std::size_t terrainTileOwnPaint(const Body &tile, const TerrainPaintRegions &regions) {
    std::vector<std::size_t> candidates;
    if (!regions.empty() && !tile.surface.vertices.empty()) {
        auto box = emptyBox();
        for (const auto &[id, p] : tile.surface.vertices)
            extend(box, p.x, p.y);
        candidates = regions.candidates(box);
    }
    std::size_t own = 0;
    for (const auto &[id, face] : tile.surface.faces) {
        const bool painted = tile.faceColors.contains(id) || tile.faceMaterials.contains(id) ||
                             tile.faceTextureMappings.contains(id);
        if (!painted)
            continue;
        if (tile.faceColors.contains(id) || tile.faceTextureMappings.contains(id) ||
            candidates.empty() || face.loops.empty() || face.loops.front().size() != 3) {
            ++own;
            continue;
        }
        const auto c = faceCentroid(tile, face);
        const auto material = regions.materialAt(c[0], c[1], candidates);
        own += !material || tile.faceMaterials.at(id) != MaterialSides{material, material};
    }
    return own;
}
std::size_t terrainPaintBandEdges(const TerrainRecord &record) {
    std::size_t sum = 0;
    for (const auto &edit : record.edits) {
        if (edit->kind != terrainDrapeKind)
            continue;
        const auto *data = dynamic_cast<const TerrainDrapeData *>(edit->data.get());
        if (!data)
            continue;
        for (const auto &paint : data->paint)
            for (const auto &loop : paint.loops)
                sum += terrainSculptRegionBands(loop).edges;
    }
    return sum;
}
} // namespace sketchy
