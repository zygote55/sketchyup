#include "core/terrain_stamp.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_mesh_view.hpp"
#include "core/terrain_plan_index.hpp"
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
double planDistance(TerrainVec2 a, TerrainVec2 b) { return planLength(b.x - a.x, b.y - a.y); }
// -0.0 is stored as +0.0 (Qt's JSON writes it as 0, so a reopened entry would differ).
double clean(double v) { return v + 0.0; }
const TerrainStampData &dataOf(const TerrainEdit &edit) {
    return dynamic_cast<const TerrainStampData &>(*edit.data);
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
bool boxesMeet(const Box &a, const Box &b, double slack = 0) {
    return !(a[2] + slack < b[0] || b[2] + slack < a[0] || a[3] + slack < b[1] ||
             b[3] + slack < a[1]);
}
// Twice the signed area, as a binary64 sum in point order (the same bits on every platform).
double signedArea2(const std::vector<Plan2> &polygon) {
    double area = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
        area += a[0] * b[1] - b[0] * a[1];
    }
    return area;
}
bool finitePoint(double x, double y) {
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= localBound &&
           std::abs(y) <= localBound;
}

// ---- Problems (the import's report kinds, located in the terrain frame) ----------------------
class Problems {
  public:
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
                                 ("Stamp pad refused; nothing was changed: " + kinds).substr(0, 256),
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
    std::vector<TerrainEditProblem> list_;
};
constexpr const char *outsideOutline = "outside-outline";
constexpr const char *leavesMessage =
    "A grading ray or the daylight line leaves the terrain; pads stay inside the terrain's "
    "outline and outside its holes.";
constexpr const char *insidePad = "inside-pad";
constexpr const char *padMessage =
    "The daylight polygon enters another building pad; move the footprint or remove that "
    "pad's stamp first.";
constexpr const char *limitKind = "limit-exceeded";
} // namespace

// ---- Data -----------------------------------------------------------------------------------
const char *terrainStampModeName(TerrainStampMode mode) {
    return mode == TerrainStampMode::Width ? "width" : "daylight";
}
bool TerrainStampData::equals(const TerrainEditData &other) const {
    const auto *o = dynamic_cast<const TerrainStampData *>(&other);
    return o && o->footprint == footprint && o->elevation == elevation && o->mode == mode &&
           o->cutSlope == cutSlope && o->fillSlope == fillSlope && o->maxDistance == maxDistance &&
           o->width == width && o->sourceBody == sourceBody && o->sourceFace == sourceFace &&
           o->outline == outline && o->rays == rays && o->dropped == dropped &&
           o->pieces == pieces;
}
std::size_t TerrainStampData::bytes() const {
    return sizeof(TerrainStampData) + (footprint.size() + outline.size()) * sizeof(Plan2) +
           rays.size() * sizeof(TerrainStampRay) + pieces.size() * sizeof(pieces[0]);
}
void validateTerrainStampData(const TerrainStampData &data) {
    auto fail = [](const char *message) { throw std::invalid_argument(message); };
    const auto n = data.footprint.size();
    if (n < 3 || n > terrainStampFootprintPoints)
        fail("A pad footprint has 3 to 20,000 points");
    for (std::size_t i = 0; i < n; ++i) {
        const auto &p = data.footprint[i];
        if (!finitePoint(p[0], p[1]))
            fail("Pad footprint points are finite and within 1,000 km of the terrain origin");
        if (p == data.footprint[(i + 1) % n])
            fail("A pad footprint repeats a point; give each corner once");
    }
    if (!(signedArea2(data.footprint) > 0))
        fail("A pad footprint runs counter-clockwise around a positive area");
    if (terrainSculptRegionBands(data.footprint).edges > terrainSculptBandEdges)
        fail("A pad footprint crosses one horizontal band more than 512 times; simplify it");
    if (!std::isfinite(data.elevation) || std::abs(data.elevation) > localBound)
        fail("The pad elevation is finite and within 1,000 km of the terrain origin");
    if (data.mode == TerrainStampMode::Daylight) {
        for (const double slope : {data.cutSlope, data.fillSlope})
            if (!std::isfinite(slope) || slope < terrainStampSlopeMin || slope > terrainStampSlopeMax)
                fail("Cut and fill slopes are 0.01 to 100 (rise over run)");
        if (!std::isfinite(data.maxDistance) || !(data.maxDistance > 0) ||
            data.maxDistance > terrainStampDistanceMax)
            fail("The maximum daylight distance is above 0 and at most 1,000 m");
        if (data.width != 0)
            fail("A daylight pad has slopes, not a width");
    } else if (data.mode == TerrainStampMode::Width) {
        if (!std::isfinite(data.width) || data.width < terrainStampWidthMin ||
            data.width > terrainStampDistanceMax)
            fail("The band width is 0.01 to 1,000 m");
        if (data.cutSlope != 0 || data.fillSlope != 0 || data.maxDistance != 0)
            fail("A fixed-width pad has a width, not slopes or a distance");
    } else
        fail("The grading mode is daylight or width");
    if (data.sourceFace && !data.sourceBody)
        fail("A pad's source face needs its source body");
    // Recorded pieces stay with an unresolved pad (terrain.update_edit keeps them), so split
    // source lines keep their ids when it applies again.
    auto checkPieces = [&] {
        if (data.pieces.size() > terrainSculptPiecesPerEdit)
            fail("A pad records at most 4,096 source line pieces");
        std::set<std::uint32_t> ids;
        for (const auto &[parent, id] : data.pieces)
            if (!parent || id < terrainSplitSourceIdFloor || !ids.insert(id).second)
                fail("A pad's source line pieces name a source and distinct piece ids from "
                     "3,221,225,472 up");
    };
    if (!data.resolved()) {
        if (!data.rays.empty() || data.dropped)
            fail("An unresolved pad has no rays");
        checkPieces();
        return;
    }
    const auto m = data.outline.size();
    if (m < 3 || m > terrainStampOutlinePoints)
        fail("A pad outline has 3 to 52,768 points");
    for (const auto &p : data.outline)
        if (!finitePoint(p[0], p[1]))
            fail("Pad outline points are finite and within the local bound");
    if (data.rays.size() < 3 || data.rays.size() > terrainStampRays)
        fail("A pad has 3 to 32,768 rays");
    std::uint32_t previous = 0;
    for (const auto &ray : data.rays) {
        if (ray.start >= m || ray.start < previous)
            fail("Pad rays start at outline points, in order");
        previous = ray.start;
        if (!finitePoint(ray.daylight.x, ray.daylight.y) || !std::isfinite(ray.daylight.z) ||
            std::abs(ray.daylight.z) > localBound)
            fail("Daylight points are finite and within the local bound");
    }
    checkPieces();
    // The daylight line bounds the work of everything that classifies points against it (the
    // clipping in the derivation of the current sources included), as the footprint does.
    std::vector<Plan2> ring;
    for (const auto &p : terrainStampRing(data))
        ring.push_back({p.x, p.y});
    if (ring.size() < 3)
        fail("A pad's daylight line has at least three distinct points");
    if (terrainSculptRegionBands(ring).edges > terrainSculptBandEdges)
        fail("A pad's daylight line crosses one horizontal band more than 512 times");
}
std::vector<Vec3> terrainStampRing(const TerrainStampData &data) {
    std::vector<Vec3> ring;
    ring.reserve(data.rays.size());
    for (const auto &ray : data.rays)
        if (ring.empty() || !(ring.back() == ray.daylight))
            ring.push_back(ray.daylight);
    while (ring.size() > 1 && ring.back() == ring.front())
        ring.pop_back();
    return ring;
}
std::array<double, 4> terrainStampBounds(const TerrainStampData &data) {
    auto box = emptyBox();
    if (data.resolved()) {
        for (const auto &p : data.outline)
            extend(box, p[0], p[1]);
        for (const auto &ray : data.rays)
            extend(box, ray.daylight.x, ray.daylight.y);
        return box;
    }
    for (const auto &p : data.footprint)
        extend(box, p[0], p[1]);
    if (data.footprint.empty())
        return {0, 0, 0, 0};
    const double reach = data.mode == TerrainStampMode::Width ? data.width : data.maxDistance;
    return {box[0] - reach, box[1] - reach, box[2] + reach, box[3] + reach};
}
std::optional<std::uint32_t> terrainStampOutlineSource(std::uint32_t edit) {
    return terrainSplitSourceId(edit, 0);
}
std::optional<std::uint32_t> terrainStampDaylightSource(std::uint32_t edit) {
    return terrainSplitSourceId(edit, 1);
}

// ---- The solver -------------------------------------------------------------------------------
namespace {
struct Ray {
    std::uint32_t start{}; // outline index
    TerrainVec2 s{};       // start (plan)
    TerrainVec2 dir{};     // unit direction
    double t{};            // plan distance to the daylight point
    Vec3 daylight;
    int mode{}; // +1 fill, -1 cut, 0 touching
};
TerrainVec2 normalize(TerrainVec2 v) {
    const double length = planLength(v.x, v.y);
    return {v.x / length, v.y / length};
}
// The rays and the outline of a counter-clockwise footprint, before any walk. `starts[i]` are
// the interior ray starts of edge i (from point i to point i + 1) as increasing fractions of it.
void layRays(const std::vector<Plan2> &footprint, const std::vector<std::vector<double>> &starts,
             std::vector<Plan2> &outline, std::vector<Ray> &rays, std::size_t &count,
             bool countOnly) {
    const std::size_t n = footprint.size();
    auto direction = [&](std::size_t i) { // unit direction of edge i
        const auto &a = footprint[i], &b = footprint[(i + 1) % n];
        return normalize({b[0] - a[0], b[1] - a[1]});
    };
    auto normal = [](TerrainVec2 d) { return TerrainVec2{d.y, -d.x}; }; // outward (CCW)
    count = 0;
    auto add = [&](std::uint32_t start, TerrainVec2 s, TerrainVec2 dir) {
        ++count;
        if (!countOnly)
            rays.push_back({start, s, dir, 0, {}, 0});
    };
    for (std::size_t i = 0; i < n; ++i) {
        const auto &p = footprint[i];
        const auto start = std::uint32_t(countOnly ? 0 : outline.size());
        if (!countOnly)
            outline.push_back(p);
        const TerrainVec2 s{p[0], p[1]};
        const auto din = direction((i + n - 1) % n), dout = direction(i);
        const auto nin = normal(din), nout = normal(dout);
        const int turn = orient(v2(footprint[(i + n - 1) % n]), s, v2(footprint[(i + 1) % n]));
        const double dot = nin.x * nout.x + nin.y * nout.y;
        // The outward bisector: by the normals' sum unless they nearly oppose (a sharp corner),
        // then by the edge directions' difference.
        auto bisector = [&]() -> TerrainVec2 {
            if (dot >= 0)
                return normalize({nin.x + nout.x, nin.y + nout.y});
            return turn > 0 ? normalize({din.x - dout.x, din.y - dout.y})
                            : normalize({dout.x - din.x, dout.y - din.y});
        };
        const double chord = planLength(nout.x - nin.x, nout.y - nin.y);
        if (turn > 0 && chord >= 1e-3) {
            // A convex corner: a fan from one edge normal to the other through the bisector, each
            // half (at most 90°) cut into steps of at most 15° by normalized interpolation.
            const auto mid = bisector();
            for (int half = 0; half < 2; ++half) {
                const auto from = half ? mid : nin, to = half ? nout : mid;
                const double c = planLength(to.x - from.x, to.y - from.y);
                // The angle is at most c * pi / 2; normalized interpolation's largest step is at
                // most 1.28 times its mean, so 8 * c steps keep every step at or below 15°.
                const auto steps = std::max<std::size_t>(1, std::size_t(std::ceil(c * 8)));
                for (std::size_t j = half ? 1 : 0; j <= steps; ++j) {
                    const double f = double(j) / double(steps);
                    add(start, s,
                        normalize({from.x * (1 - f) + to.x * f, from.y * (1 - f) + to.y * f}));
                }
            }
        } else if (turn != 0 || chord >= 1e-3)
            add(start, s, bisector()); // a concave corner, or an almost straight one
        else
            add(start, s, nout); // straight
        // Interior points of edge i.
        const auto &b = footprint[(i + 1) % n];
        for (const double f : starts[i]) {
            const Plan2 q{clean(p[0] + (b[0] - p[0]) * f), clean(p[1] + (b[1] - p[1]) * f)};
            const auto index = std::uint32_t(countOnly ? 0 : outline.size());
            if (!countOnly)
                outline.push_back(q);
            add(index, {q[0], q[1]}, nout);
            if (count > terrainStampRays && countOnly)
                return;
        }
        if (count > terrainStampRays && countOnly)
            return;
    }
}

// The ground along a ray: the intervals of t in which S + t * dir lies in a triangle (closed,
// widened by rounding), with the triangle's plane along the ray (g0 + g1 * t).
struct Interval {
    double t0, t1, g0, g1;
    TerrainTriangleRef ref;
    bool operator<(const Interval &o) const {
        return std::tie(t0, t1, ref) < std::tie(o.t0, o.t1, o.ref);
    }
};
class Walker {
  public:
    Walker(const TerrainEditMesh &mesh, std::uint64_t &work, TerrainProgress *progress)
        : mesh_(mesh), work_(work), progress_(progress) {}
    // Walks one ray. Daylight mode (width = 0): the first crossing of the grade with the ground;
    // fixed width: the ground at `width`. Throws through `problems`.
    void walk(Ray &ray, double h, double fill, double cut, double maxDistance, double width,
              Problems &problems) {
        const double reachTo = width > 0 ? width : maxDistance;
        const double cell = mesh_.grid().cell > 0 ? mesh_.grid().cell : reachTo;
        const auto steps = std::size_t(std::clamp(std::ceil(reachTo / cell), 1.0, 1e7));
        const TerrainVec2 s = ray.s, d = ray.dir;
        std::vector<Interval> pending;
        std::unordered_set<const void *> seen;
        // Covered from 0 to `reach` without a gap; `best` is the first crossing found so far.
        double reach = 0, best = std::numeric_limits<double>::infinity();
        bool started = false, decided = false, done = false;
        double slope = 0;
        int mode = 0;
        Interval at{}; // fixed width: the first interval holding t = width
        bool atFound = false;
        constexpr double gap = 1e-9;
        auto point = [&](double t) { return TerrainVec2{s.x + t * d.x, s.y + t * d.y}; };
        auto outside = [&] {
            problems.refuse(outsideOutline,
                            "A footprint point is outside the terrain; pads stay inside the "
                            "terrain's outline and outside its holes.",
                            s);
        };
        auto grade = [&](double t) { return mode > 0 ? h - slope * t : h + slope * t; };
        for (std::size_t step = 0; step < steps && !done; ++step) {
            const double ta = reachTo * double(step) / double(steps),
                         tb = step + 1 == steps ? reachTo : reachTo * double(step + 1) / double(steps);
            const auto pa = point(ta), pb = point(tb);
            // Each step counts as work too, so a long walk over few large tiles is bounded.
            charge(1, s);
            const std::size_t sorted = pending.size();
            for (const auto *tile : mesh_.tilesIn(std::min(pa.x, pb.x), std::min(pa.y, pb.y),
                                                  std::max(pa.x, pb.x), std::max(pa.y, pb.y), 1e-6))
                if (seen.insert(tile).second)
                    add(*tile, s, d, reachTo, pending);
            // Every interval meeting this step is known now (its triangle's tile meets the step's
            // box), so those starting by its end are processed in order.
            // Only the new intervals are sorted; the rest are in order already.
            std::sort(pending.begin() + std::ptrdiff_t(sorted), pending.end());
            std::inplace_merge(pending.begin(), pending.begin() + std::ptrdiff_t(sorted),
                               pending.end());
            std::size_t used = 0;
            for (; used < pending.size() && pending[used].t0 <= tb + gap && !done; ++used) {
                const auto &iv = pending[used];
                if (!started) {
                    if (iv.t0 > gap)
                        outside();
                    started = true;
                }
                if (iv.t0 > reach + gap) {
                    if (decided && width <= 0 && best <= reach + gap) {
                        done = true;
                        break;
                    }
                    problems.refuse(outsideOutline, leavesMessage, point(reach));
                }
                if (!decided) {
                    // The grade's direction from the ground at the start (the first interval).
                    const double g = iv.g0 + iv.g1 * std::max(iv.t0, 0.0);
                    decided = true;
                    mode = h > g ? 1 : h < g ? -1 : 0;
                    slope = mode > 0 ? fill : cut;
                    if (width <= 0 && mode == 0) {
                        best = 0;
                        done = true;
                        break;
                    }
                }
                reach = std::max(reach, iv.t1);
                if (width > 0) {
                    if (!atFound && iv.t0 <= width && width <= iv.t1) {
                        at = iv;
                        atFound = true;
                    }
                    continue;
                }
                // The grade minus the ground; the crossing is where it leaves the start's sign.
                const double t0 = std::max(iv.t0, 0.0), t1 = iv.t1;
                const double d0 = grade(t0) - (iv.g0 + iv.g1 * t0),
                             d1 = grade(t1) - (iv.g0 + iv.g1 * t1);
                const bool crossed0 = mode > 0 ? d0 <= 0 : d0 >= 0,
                           crossed1 = mode > 0 ? d1 <= 0 : d1 >= 0;
                if (crossed0)
                    best = std::min(best, t0);
                else if (crossed1) {
                    // Exact on this triangle's plane: h -/+ slope * t = g0 + g1 * t.
                    const double t = mode > 0 ? (h - iv.g0) / (slope + iv.g1)
                                              : (iv.g0 - h) / (slope - iv.g1);
                    best = std::min(best, std::clamp(std::isfinite(t) ? t : t1, t0, t1));
                }
            }
            pending.erase(pending.begin(), pending.begin() + std::ptrdiff_t(used));
            if (done)
                break;
            if (!started)
                outside();
            // A crossing within the covered range and this step: no later interval comes first.
            if (width <= 0 && best <= reach + gap && best <= tb + gap)
                break;
            if (width > 0 && atFound && reach >= width - gap && width <= tb + gap)
                break;
            // Every interval starting by this step's end is processed: coverage that stops short
            // of it is the ray leaving the terrain.
            if (reach < tb - gap) {
                if (width <= 0 && best <= reach + gap)
                    break;
                problems.refuse(outsideOutline, leavesMessage, point(reach));
            }
        }
        if (width > 0) {
            if (!atFound)
                problems.refuse(outsideOutline, leavesMessage, point(width));
            const double z = at.g0 + at.g1 * width;
            ray.t = width;
            ray.mode = h > z ? 1 : h < z ? -1 : 0;
            const auto p = point(width);
            ray.daylight = {clean(p.x), clean(p.y), clean(z)};
            return;
        }
        if (!std::isfinite(best))
            problems.refuse("daylight-distance",
                            "A grading ray reaches the maximum daylight distance without meeting "
                            "the ground; raise the maximum distance or steepen the slopes.",
                            point(maxDistance));
        if (mode == 0 || best * planLength(d.x, d.y) < terrainStampTouch) {
            ray.t = 0;
            ray.mode = 0;
            ray.daylight = {s.x, s.y, h};
            return;
        }
        ray.t = best;
        ray.mode = mode;
        const auto p = point(best);
        ray.daylight = {clean(p.x), clean(p.y), clean(grade(best))};
    }

  private:
    void add(const TerrainEditMesh::Tile &tile, TerrainVec2 s, TerrainVec2 d, double reachTo,
             std::vector<Interval> &out) {
        const auto &content = *tile.content;
        const double ex = s.x + d.x * reachTo, ey = s.y + d.y * reachTo;
        const double minX = std::min(s.x, ex), maxX = std::max(s.x, ex), minY = std::min(s.y, ey),
                     maxY = std::max(s.y, ey);
        const double margin = 1e-6 * (std::abs(d.x) + std::abs(d.y)) * reachTo + 1e-9;
        charge(content.triangles.size(), s);
        if ((++tiles_ & 63) == 0)
            terrainCheckpoint(progress_, TerrainStage::Replay, 0); // cancellation only
        for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
            const auto &tri = content.triangles[t];
            const Vec3 &a = content.vertices[tri[0]], &b = content.vertices[tri[1]],
                       &c = content.vertices[tri[2]];
            if (maxX < std::min({a.x, b.x, c.x}) - 1e-9 || minX > std::max({a.x, b.x, c.x}) + 1e-9 ||
                maxY < std::min({a.y, b.y, c.y}) - 1e-9 || minY > std::max({a.y, b.y, c.y}) + 1e-9)
                continue;
            const auto side = [&](const Vec3 &v) { return d.x * (v.y - s.y) - d.y * (v.x - s.x); };
            const double sa = side(a), sb = side(b), sc = side(c);
            if ((sa > margin && sb > margin && sc > margin) ||
                (sa < -margin && sb < -margin && sc < -margin))
                continue;
            double lo = 0, hi = reachTo;
            bool empty = false;
            const Vec3 *corner[3] = {&a, &b, &c};
            for (int e = 0; e < 3 && !empty; ++e) {
                const Vec3 &u = *corner[e], &v = *corner[(e + 1) % 3];
                const double ux = v.x - u.x, uy = v.y - u.y;
                const double alpha = ux * (s.y - u.y) - uy * (s.x - u.x);
                const double beta = ux * d.y - uy * d.x;
                const double eta = 1e-10 * planLength(ux, uy); // widened by about 1e-10 m
                if (beta == 0) {
                    if (alpha < -eta)
                        empty = true;
                } else if (beta > 0)
                    lo = std::max(lo, (-eta - alpha) / beta);
                else
                    hi = std::min(hi, (-eta - alpha) / beta);
                empty = empty || lo > hi;
            }
            if (empty)
                continue;
            // The triangle's plane along the ray (counter-clockwise, so the normal's z is positive).
            const double nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
            const double ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
            const double nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (!(nz > 0))
                continue;
            const double g0 = a.z - (nx * (s.x - a.x) + ny * (s.y - a.y)) / nz;
            const double g1 = -(nx * d.x + ny * d.y) / nz;
            out.push_back({lo, hi, g0, g1, {content.key, t}});
        }
    }
    void charge(std::uint64_t amount, TerrainVec2 at) {
        work_ += amount;
        if (work_ > terrainStampWalkWork)
            throw TerrainEditRefused(
                "LIMIT_EXCEEDED",
                "Stamp pad refused; nothing was changed: the grading rays cross too much of the "
                "terrain; use a smaller footprint, maximum distance or width",
                {{limitKind,
                  "The grading rays would test more than 67,108,864 triangles; use a smaller "
                  "footprint, maximum distance or width.",
                  1,
                  {{at, 0, 0}}}});
    }
    const TerrainEditMesh &mesh_;
    std::uint64_t &work_;
    TerrainProgress *progress_;
    std::uint64_t tiles_{};
};

// Segments for the repair: the outline pieces, the rays with length and the daylight edges.
struct Segment {
    TerrainVec2 a, b;
    int kind;           // 0 outline, 1 ray, 2 daylight edge
    std::uint32_t r0, r1; // ray positions (in the kept list) it belongs to
};
// Every pair of segments that meet other than at a shared end point (exact), found through a
// uniform grid: each segment is listed in the cells it passes through (a grid walk, with both
// neighbours where it passes a corner), so two segments that meet share a cell. Outline pieces are
// not paired with each other. With `cyclic` (a polygon's n edges, segment i from corner i), a
// shared corner is allowed only between neighbouring edges. The cell is at least the median
// segment length and large enough that the walks list at most about five cells a segment; the
// pairs are counted before any is tested, and more than terrainStampPairWork refuses the call
// (located at the first segment, whatever the hash order).
std::vector<std::pair<std::uint32_t, std::uint32_t>> crossings(const std::vector<Segment> &segments,
                                                               Problems &problems,
                                                               TerrainProgress *progress,
                                                               std::size_t cyclic = 0) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
    if (segments.empty())
        return result;
    auto box = emptyBox();
    std::vector<double> lengths;
    lengths.reserve(segments.size());
    double total = 0;
    for (const auto &s : segments) {
        extend(box, s.a.x, s.a.y);
        extend(box, s.b.x, s.b.y);
        lengths.push_back(planDistance(s.a, s.b));
        total += lengths.back();
    }
    std::nth_element(lengths.begin(), lengths.begin() + std::ptrdiff_t(lengths.size() / 2),
                     lengths.end());
    const double span = std::max(box[2] - box[0], box[3] - box[1]);
    const double cell = std::max({lengths[lengths.size() / 2], total / (4 * double(segments.size())),
                                  span / 4096, 1e-6});
    auto refuse = [&] {
        problems.refuse(limitKind,
                        "The grading is too irregular to check for crossings; use a smaller "
                        "footprint, maximum distance or width.",
                        segments.front().a);
    };
    std::uint64_t work = 0;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> grid;
    auto key = [](std::int64_t x, std::int64_t y) {
        return (std::uint64_t(std::uint32_t(x)) << 32) | std::uint32_t(y);
    };
    auto cellOf = [&](double v, double low) {
        return std::int64_t(std::floor((v - low) / cell));
    };
    auto list = [&](std::int64_t x, std::int64_t y, std::uint32_t i) {
        auto &cellList = grid[key(x, y)];
        if (cellList.empty() || cellList.back() != i)
            cellList.push_back(i);
        if (++work > terrainStampPairWork)
            refuse();
    };
    for (std::uint32_t i = 0; i < segments.size(); ++i) {
        const auto &s = segments[i];
        // Amanatides and Woo's walk in cell units; where it passes a corner (both crossings within
        // a hair) both neighbours are listed too.
        const double ax = (s.a.x - box[0]) / cell, ay = (s.a.y - box[1]) / cell,
                     bx = (s.b.x - box[0]) / cell, by = (s.b.y - box[1]) / cell;
        std::int64_t x = cellOf(s.a.x, box[0]), y = cellOf(s.a.y, box[1]);
        const std::int64_t endX = cellOf(s.b.x, box[0]), endY = cellOf(s.b.y, box[1]);
        const double dx = bx - ax, dy = by - ay;
        const int stepX = dx > 0 ? 1 : dx < 0 ? -1 : 0, stepY = dy > 0 ? 1 : dy < 0 ? -1 : 0;
        const double inf = std::numeric_limits<double>::infinity();
        double tMaxX = stepX ? ((stepX > 0 ? double(x + 1) : double(x)) - ax) / dx : inf;
        double tMaxY = stepY ? ((stepY > 0 ? double(y + 1) : double(y)) - ay) / dy : inf;
        const double tDeltaX = stepX ? std::abs(1 / dx) : inf, tDeltaY = stepY ? std::abs(1 / dy) : inf;
        list(x, y, i);
        const auto cells = std::abs(endX - x) + std::abs(endY - y) + 2;
        for (std::int64_t k = 0; k < cells * 2 && (x != endX || y != endY); ++k) {
            if (std::abs(tMaxX - tMaxY) <= 1e-9) {
                list(x + stepX, y, i);
                list(x, y + stepY, i);
                x += stepX;
                y += stepY;
                tMaxX += tDeltaX;
                tMaxY += tDeltaY;
            } else if (tMaxX < tMaxY) {
                x += stepX;
                tMaxX += tDeltaX;
            } else {
                y += stepY;
                tMaxY += tDeltaY;
            }
            list(x, y, i);
        }
        // Rounding may stop the walk a cell short: the end's cell is listed in any case.
        list(endX, endY, i);
    }
    // The pairs to test, counted first (the count does not depend on the hash order).
    for (const auto &[cellKey, cellList] : grid) {
        std::uint64_t outline = 0;
        for (const auto i : cellList)
            outline += segments[i].kind == 0;
        const std::uint64_t others = cellList.size() - outline;
        work += outline * others + others * (others > 0 ? others - 1 : 0) / 2;
        if (work > terrainStampPairWork)
            refuse();
    }
    terrainCheckpoint(progress, TerrainStage::Replay, 0);
    auto same = [](TerrainVec2 p, TerrainVec2 q) { return p.x == q.x && p.y == q.y; };
    auto neighbours = [&](const Segment &s, const Segment &t) {
        const auto d = s.r0 > t.r0 ? s.r0 - t.r0 : t.r0 - s.r0;
        return d == 1 || d + 1 == cyclic;
    };
    auto crossing = [&](std::uint32_t i, std::uint32_t j) {
        const auto &s = segments[i], &t = segments[j];
        if (s.kind == 0 && t.kind == 0)
            return false; // the footprint is simple
        if (!terrainSegmentsMeet(s.a, s.b, t.a, t.b))
            return false;
        // Identical segments (a daylight edge along the outline where the pad meets the ground)
        // are one constraint.
        if ((same(s.a, t.a) && same(s.b, t.b)) || (same(s.a, t.b) && same(s.b, t.a)))
            return cyclic != 0;
        // A shared end point is allowed when the segments do not overlap along a line (for a
        // polygon's own edges, only between neighbours).
        std::optional<TerrainVec2> shared, otherS, otherT;
        if (same(s.a, t.a)) {
            shared = s.a;
            otherS = s.b;
            otherT = t.b;
        } else if (same(s.a, t.b)) {
            shared = s.a;
            otherS = s.b;
            otherT = t.a;
        } else if (same(s.b, t.a)) {
            shared = s.b;
            otherS = s.a;
            otherT = t.b;
        } else if (same(s.b, t.b)) {
            shared = s.b;
            otherS = s.a;
            otherT = t.a;
        }
        if (!shared)
            return true;
        if (cyclic && !neighbours(s, t))
            return true;
        const bool collinear = orient(*shared, *otherS, *otherT) == 0;
        const bool sameSide = (otherS->x - shared->x) * (otherT->x - shared->x) +
                                  (otherS->y - shared->y) * (otherT->y - shared->y) >
                              0;
        return collinear && sameSide;
    };
    // Tested cell by cell; a pair sharing several cells is found once (sorted, unique).
    for (const auto &[cellKey, cellList] : grid) {
        terrainCheckpoint(progress, TerrainStage::Replay, 0); // cancellation only
        for (std::size_t x = 0; x < cellList.size(); ++x)
            for (std::size_t y = x + 1; y < cellList.size(); ++y) {
                const auto i = std::min(cellList[x], cellList[y]),
                           j = std::max(cellList[x], cellList[y]);
                if (crossing(i, j))
                    result.push_back({i, j});
            }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
} // namespace

TerrainStampPlan planTerrainStamp(const TerrainEditContext &context, const TerrainEdit &edit) {
    const auto &data = dataOf(edit);
    const auto &mesh = context.mesh;
    const auto &state = context.sources;
    TerrainMeshView view(mesh);
    Problems problems;
    TerrainStampPlan plan;
    const double h = data.elevation;
    const auto &footprint = data.footprint;
    const std::size_t n = footprint.size();
    const bool daylight = data.mode == TerrainStampMode::Daylight;
    TerrainProgress *progress = context.progress;
    const auto outlineSource = terrainStampOutlineSource(edit.id),
               daylightSource = terrainStampDaylightSource(edit.id);
    if (!outlineSource || !daylightSource)
        problems.refuse(limitKind,
                        "The terrain has used too many change IDs to stamp more pads; Explode "
                        "terrain keeps the surface as ordinary geometry",
                        v2(footprint.front()));
    for (const auto id : {*outlineSource, *daylightSource})
        if (state.find(id))
            problems.refuse(limitKind,
                            "A pad source would take the id " + std::to_string(id) +
                                ", which a source already has",
                            v2(footprint.front()));
    // ---- The footprint: simple (no two edges meet but neighbours at their shared corner) ----
    {
        std::vector<Segment> edges;
        for (std::uint32_t i = 0; i < n; ++i)
            edges.push_back({v2(footprint[i]), v2(footprint[(i + 1) % n]), 1, i, i});
        // kind 1 so they are compared with each other; neighbours share a corner.
        for (const auto &[i, j] : crossings(edges, problems, progress, n))
            problems.add("invalid-polygon",
                         "A pad footprint must be a simple polygon (its edges may not cross or "
                         "touch).",
                         edges[j].a, i + 1, j + 1);
        for (std::uint32_t i = 0; i < n; ++i)
            if (planDistance(edges[i].a, edges[i].b) < terrainStampSlack)
                problems.add("invalid-polygon",
                             "A pad footprint's corners are at least 1e-6 m apart.", edges[i].a);
        problems.check();
    }
    std::uint64_t walkWork = 0;
    // ---- Edge spacing: along each stretch of an edge (a quarter cell long), no larger than the
    // smallest longest side of the triangles that stretch touches, so one sliver makes rays dense
    // only where it is ----
    std::vector<std::vector<double>> starts(n);
    std::size_t interior = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if ((i & 255) == 0)
            terrainCheckpoint(progress, TerrainStage::Replay, 0);
        const auto a = v2(footprint[i]), b = v2(footprint[(i + 1) % n]);
        const double length = planDistance(a, b);
        // An edge no longer than the smallest spacing has no interior rays at any spacing.
        if (length <= terrainStampSpacingMin)
            continue;
        const double cell = mesh.grid().cell > 0 ? mesh.grid().cell : 1.0;
        // The triangles the query scans (its tiles along the edge) count against the walk's bound.
        {
            const auto steps = std::size_t(std::clamp(std::ceil(length / cell), 1.0, 1e6));
            std::unordered_set<const void *> tiles;
            std::uint64_t triangles = steps;
            for (std::size_t k = 0; k < steps; ++k) {
                const double f0 = double(k) / double(steps), f1 = double(k + 1) / double(steps);
                const double x0 = a.x + (b.x - a.x) * f0, y0 = a.y + (b.y - a.y) * f0,
                             x1 = a.x + (b.x - a.x) * f1, y1 = a.y + (b.y - a.y) * f1;
                for (const auto *tile : mesh.tilesIn(std::min(x0, x1), std::min(y0, y1),
                                                     std::max(x0, x1), std::max(y0, y1), 1e-6))
                    if (tiles.insert(tile).second)
                        triangles += tile->content->triangles.size();
            }
            walkWork += triangles;
            if (walkWork > terrainStampWalkWork)
                problems.refuse(limitKind,
                                "The footprint's edges cross too much of the terrain; use a "
                                "simpler or smaller footprint.",
                                a);
        }
        const auto chunks = std::size_t(
            std::clamp(std::ceil(length / std::max(cell / 4, terrainStampSpacingMin)), 1.0,
                       double(terrainStampRays) + 1));
        std::vector<double> size(chunks, std::numeric_limits<double>::infinity());
        const double dx = b.x - a.x, dy = b.y - a.y, length2 = dx * dx + dy * dy;
        for (const auto &ref : view.touching(a, b)) {
            const auto c = view.corners(ref);
            double longest = 0, lo = 1, hi = 0;
            for (int k = 0; k < 3; ++k) {
                const auto &p = c[std::size_t(k)];
                longest = std::max(longest, planDistance(v2(p), v2(c[std::size_t((k + 1) % 3)])));
                const double u = ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2;
                lo = std::min(lo, u);
                hi = std::max(hi, u);
            }
            // The stretches the triangle's projection on the edge covers.
            const auto first = std::size_t(std::clamp(std::floor(lo * double(chunks)), 0.0,
                                                      double(chunks - 1)));
            const auto last = std::size_t(std::clamp(std::floor(hi * double(chunks)), 0.0,
                                                     double(chunks - 1)));
            for (auto k = first; k <= last; ++k)
                size[k] = std::min(size[k], longest);
        }
        const double chunk = length / double(chunks);
        for (std::size_t k = 0; k < chunks; ++k) {
            if (k)
                starts[i].push_back(double(k) / double(chunks));
            const double step = std::max(std::isfinite(size[k]) ? size[k] : chunk,
                                         terrainStampSpacingMin);
            const auto pieces = std::size_t(
                std::clamp(std::ceil(chunk / step), 1.0, double(terrainStampRays) + 1));
            for (std::size_t j = 1; j < pieces; ++j)
                starts[i].push_back((double(k) + double(j) / double(pieces)) / double(chunks));
            if (interior + starts[i].size() > terrainStampRays)
                problems.refuse(limitKind,
                                "The footprint needs more than 32,768 grading rays at this "
                                "terrain's triangle size; use a simpler or smaller footprint.",
                                a);
        }
        interior += starts[i].size();
    }
    // ---- Rays ----
    std::vector<Plan2> outline;
    std::vector<Ray> rays;
    std::size_t count = 0;
    layRays(footprint, starts, outline, rays, count, true);
    if (count > terrainStampRays)
        problems.refuse(limitKind,
                        "The footprint needs more than 32,768 grading rays at this terrain's "
                        "triangle size; use a simpler or smaller footprint.",
                        v2(footprint.front()));
    layRays(footprint, starts, outline, rays, count, false);
    plan.rays = rays.size();
    Walker walker(mesh, walkWork, progress);
    for (std::size_t i = 0; i < rays.size(); ++i) {
        if ((i & 63) == 0)
            terrainCheckpoint(progress, TerrainStage::Replay, 0);
        walker.walk(rays[i], h, data.fillSlope, data.cutSlope, data.maxDistance,
                    daylight ? 0 : data.width, problems);
    }
    // ---- Self-intersection repair ----
    std::vector<std::uint32_t> kept;
    auto dropAt = [&](std::uint32_t ray) {
        if (!plan.dropped)
            plan.droppedAt = v2(rays[ray].daylight);
    };
    for (std::uint32_t i = 0; i < rays.size(); ++i) {
        // A daylight point within 1e-6 m of the one before it (a fan's rays on a short reach)
        // adds nothing but a sliver: that ray is dropped.
        if (!kept.empty()) {
            const auto &previous = rays[kept.back()].daylight;
            if (!(previous == rays[i].daylight) &&
                planDistance(v2(previous), v2(rays[i].daylight)) <= terrainStampSlack) {
                dropAt(i);
                ++plan.dropped;
                continue;
            }
        }
        kept.push_back(i);
    }
    for (int round = 0;; ++round) {
        terrainCheckpoint(progress, TerrainStage::Replay, 0);
        if (kept.size() < 3)
            problems.refuse("daylight-repair",
                            "The grading crosses itself everywhere; no daylight line remains. Use "
                            "a simpler footprint or gentler slopes.",
                            v2(footprint.front()));
        if (round >= 16)
            problems.refuse("daylight-repair",
                            "The grading crosses itself too often to repair; use a simpler "
                            "footprint or gentler slopes.",
                            v2(footprint.front()));
        std::vector<Segment> segments;
        const auto m = std::uint32_t(outline.size());
        for (std::uint32_t i = 0; i < m; ++i)
            segments.push_back({v2(outline[i]), v2(outline[(i + 1) % m]), 0, 0, 0});
        const auto k = std::uint32_t(kept.size());
        for (std::uint32_t i = 0; i < k; ++i) {
            const auto &r = rays[kept[i]];
            if (!(r.daylight.x == r.s.x && r.daylight.y == r.s.y))
                segments.push_back({r.s, v2(r.daylight), 1, i, i});
            const auto &next = rays[kept[(i + 1) % k]];
            if (!(r.daylight.x == next.daylight.x && r.daylight.y == next.daylight.y))
                segments.push_back({v2(r.daylight), v2(next.daylight), 2, i, (i + 1) % k});
        }
        const auto found = crossings(segments, problems, progress);
        if (found.empty())
            break;
        std::vector<bool> drop(k, false);
        for (const auto &[i, j] : found) {
            std::vector<std::uint32_t> positions;
            for (const auto *s : {&segments[i], &segments[j]})
                if (s->kind != 0) {
                    positions.push_back(s->r0);
                    positions.push_back(s->r1);
                }
            if (positions.empty())
                continue;
            std::sort(positions.begin(), positions.end());
            positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
            // The shortest run around the ring holding them all: the complement of the largest
            // gap between consecutive positions.
            std::size_t gapAt = positions.size() - 1;
            std::uint32_t gapSize = positions.front() + k - positions.back();
            for (std::size_t p = 0; p + 1 < positions.size(); ++p)
                if (positions[p + 1] - positions[p] > gapSize) {
                    gapSize = positions[p + 1] - positions[p];
                    gapAt = p;
                }
            // The run starts after the gap and ends at its start.
            const auto first = positions[(gapAt + 1) % positions.size()], last = positions[gapAt];
            for (auto p = first;; p = (p + 1) % k) {
                drop[p] = true;
                if (p == last)
                    break;
            }
        }
        std::vector<std::uint32_t> next;
        for (std::uint32_t i = 0; i < k; ++i) {
            if (drop[i]) {
                dropAt(kept[i]);
                ++plan.dropped;
            } else
                next.push_back(kept[i]);
        }
        kept = std::move(next);
    }
    // ---- The resolved geometry ----
    auto resolved = std::make_shared<TerrainStampData>(data);
    resolved->outline = outline;
    resolved->rays.clear();
    for (const auto i : kept) {
        resolved->rays.push_back({rays[i].start, rays[i].daylight});
        plan.touching += rays[i].mode == 0;
        plan.fill += rays[i].mode > 0;
        plan.cut += rays[i].mode < 0;
    }
    resolved->dropped = std::uint32_t(plan.dropped);
    const auto ring = terrainStampRing(*resolved);
    std::vector<Plan2> ringPlan;
    for (const auto &p : ring)
        ringPlan.push_back({p.x, p.y});
    if (ringPlan.size() < 3 || !(signedArea2(ringPlan) > 0))
        problems.refuse("daylight-repair",
                        "The daylight line does not enclose the pad; use a simpler footprint or "
                        "gentler slopes.",
                        v2(footprint.front()));
    // A daylight line crossing one horizontal band more than 512 times (ground corrugated across
    // the rays) is refused, as a footprint is: everything that classifies points against it,
    // the derivation of the current sources included, reads at most one band of edges.
    if (const auto bands = terrainSculptRegionBands(ringPlan); bands.edges > terrainSculptBandEdges) {
        double x = ringPlan.front()[0];
        for (const auto &p : ringPlan)
            if (terrainSculptBandOf(bands, p[1]) == bands.busiest) {
                x = p[0];
                break;
            }
        problems.refuse(limitKind,
                        "The daylight line crosses one horizontal band " +
                            std::to_string(bands.edges) +
                            " times (at most 512): the ground is too irregular here; use fixed "
                            "width, steeper slopes or a smaller maximum distance.",
                        {x, bands.low + (double(bands.busiest) + 0.5) * bands.height});
    }
    const TerrainPolygonIndex polygon(ringPlan);
    if (!polygon.contains(footprint.front()[0], footprint.front()[1]))
        problems.refuse("daylight-repair",
                        "The daylight line does not enclose the pad; use a simpler footprint or "
                        "gentler slopes.",
                        v2(footprint.front()));
    constexpr double slack = terrainStampSlack;
    std::uint64_t classifyWork = 0;
    auto classifyCost = [&](TerrainVec2 at) {
        classifyWork += polygon.busiestBand() + 8;
        if (classifyWork > terrainStampClassifyWork)
            problems.refuse(limitKind,
                            "The daylight line is too irregular to grade here; use fixed width, "
                            "a smaller maximum distance or a smaller footprint.",
                            at);
    };
    // ---- Other pads: the widened daylight polygon may not meet their outlines ----
    {
        const TerrainPadIndex pads(state.protectedRegions);
        const auto &ringBox = polygon.box();
        auto inBox = [&](const std::array<double, 4> &box, double x, double y) {
            return x >= box[0] - slack && x <= box[2] + slack && y >= box[1] - slack &&
                   y <= box[3] + slack;
        };
        pads.inBox(polygon.box(), slack, [&](const TerrainProtectedRegion &region) {
            // Only points and edges in the other's box are tested, each counted.
            std::optional<TerrainVec2> at;
            for (const auto &p : ringPlan)
                if (inBox(region.box, p[0], p[1]) && region.index) {
                    classifyCost(v2(p));
                    if (region.index->near(p[0], p[1], slack)) {
                        at = v2(p);
                        break;
                    }
                }
            for (const auto *list : {&region.polygon, &region.points})
                for (const auto &p : *list)
                    if (!at && inBox(ringBox, p[0], p[1])) {
                        classifyCost(v2(p));
                        if (polygon.near(p[0], p[1], slack))
                            at = v2(p);
                    }
            if (!at && region.index)
                for (std::size_t i = 0; i < ringPlan.size(); ++i) {
                    const auto a = v2(ringPlan[i]), b = v2(ringPlan[(i + 1) % ringPlan.size()]);
                    if (std::max(a.x, b.x) < region.box[0] - slack ||
                        std::min(a.x, b.x) > region.box[2] + slack ||
                        std::max(a.y, b.y) < region.box[1] - slack ||
                        std::min(a.y, b.y) > region.box[3] + slack)
                        continue;
                    classifyCost(a);
                    if (region.index->segmentNear(a, b, slack)) {
                        at = TerrainVec2{(a.x + b.x) / 2, (a.y + b.y) / 2};
                        break;
                    }
                }
            if (at)
                problems.add(insidePad, padMessage, *at, 0, region.edit);
            return true;
        });
        problems.check();
    }
    // ---- The cavity: a flood from the footprint over every triangle meeting the widened
    // polygon, bounded as R096.c's ----
    std::unordered_map<TerrainPositionKey, bool, TerrainPositionHash> inside; // memo, by position
    auto removed = [&](const Vec3 &p) {
        const auto key = TerrainPositionKey::of(p);
        if (const auto found = inside.find(key); found != inside.end())
            return found->second;
        classifyCost(v2(p));
        const bool result = polygon.near(p.x, p.y, slack);
        inside.emplace(key, result);
        return result;
    };
    std::set<TerrainTriangleRef> cavity;
    std::vector<TerrainTriangleRef> queue;
    auto grow = [&](const TerrainTriangleRef &ref, TerrainVec2 where) {
        if (!cavity.insert(ref).second)
            return false;
        if (cavity.size() > terrainSourceEditCavityTriangles)
            problems.refuse(limitKind,
                            "The pad would re-triangulate more than " +
                                std::to_string(terrainSourceEditCavityTriangles) +
                                " triangles; use a smaller footprint or maximum distance.",
                            where);
        if ((cavity.size() & 1023) == 0)
            terrainCheckpoint(progress, TerrainStage::Replay, 0);
        queue.push_back(ref);
        return true;
    };
    {
        const auto start = view.containing(v2(footprint.front()));
        if (start.empty())
            problems.refuse(outsideOutline,
                            "A footprint point is outside the terrain; pads stay inside the "
                            "terrain's outline and outside its holes.",
                            v2(footprint.front()));
        for (const auto &ref : start)
            grow(ref, v2(footprint.front()));
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto ref = queue[head];
        const auto c = view.corners(ref);
        for (int i = 0; i < 3; ++i) {
            const auto &a = c[std::size_t(i)], &b = c[std::size_t((i + 1) % 3)];
            const auto next = view.across(ref, a, b);
            classifyCost(v2(a));
            const bool meets = removed(a) || removed(b) || polygon.segmentNear(v2(a), v2(b), slack);
            if (!next) {
                // An outline or hole edge the widened polygon reaches: the pad leaves the terrain.
                if (meets)
                    problems.refuse(outsideOutline, leavesMessage,
                                    {(a.x + b.x) / 2, (a.y + b.y) / 2});
                continue;
            }
            if (cavity.contains(*next))
                continue;
            bool enter = meets;
            if (!enter) {
                const auto n3 = view.corners(*next);
                for (const auto &q : n3)
                    enter = enter || removed(q);
            }
            if (enter)
                grow(*next, v2(a));
        }
    }
    // Removed vertices: those in the widened polygon. One on the outline is the pad leaving it.
    std::map<TerrainPlanKey, Vec3> removedVertices;
    for (const auto &ref : cavity)
        for (const auto &p : view.corners(ref))
            if (removed(p) && removedVertices.emplace(TerrainPlanKey::of(p.x, p.y), p).second &&
                view.boundaryVertex(p))
                problems.refuse(outsideOutline, leavesMessage, v2(p));
    // A polygon wholly inside one triangle touches no vertex or edge of it: the flood's start
    // triangle holds it, which is already in the cavity.
    // ---- Bowyer–Watson from each inserted vertex (never across a kept constraint, the outline or
    // a pad) ----
    const TerrainPadIndex pads(state.protectedRegions);
    auto constraint = [&](const Vec3 &a, const Vec3 &b) {
        return state.segmentUses(TerrainPlanKey::of(a.x, a.y), TerrainPlanKey::of(b.x, b.y)) > 0 &&
               !polygon.segmentNear(v2(a), v2(b), slack);
    };
    std::vector<Vec3> inserted;
    for (const auto &p : outline)
        inserted.push_back({p[0], p[1], h});
    for (const auto &p : ring)
        inserted.push_back(p);
    // Every inserted vertex lies in the widened polygon, so the triangles holding it are flood
    // triangles: a grid over them finds them without scanning whole tiles.
    std::vector<TerrainTriangleRef> flood(cavity.begin(), cavity.end());
    std::vector<TerrainBoxTree::Box> floodBoxes;
    floodBoxes.reserve(flood.size());
    for (const auto &ref : flood) {
        const auto c = view.corners(ref);
        floodBoxes.push_back({std::min({c[0].x, c[1].x, c[2].x}), std::min({c[0].y, c[1].y, c[2].y}),
                              std::max({c[0].x, c[1].x, c[2].x}), std::max({c[0].y, c[1].y, c[2].y})});
    }
    const TerrainBoxTree floodTree(std::move(floodBoxes));
    auto containing = [&](TerrainVec2 q) {
        std::vector<TerrainTriangleRef> found;
        floodTree.visit({q.x, q.y, q.x, q.y}, [&](std::uint32_t k) {
            const auto c = view.corners(flood[k]);
            if (terrain_view::inTriangle(v2(c[0]), v2(c[1]), v2(c[2]), q))
                found.push_back(flood[k]);
            return true;
        });
        std::sort(found.begin(), found.end());
        return found;
    };
    // The walks are bounded as R096.c bounds its touched triangles: every triangle a walk visits
    // counts.
    std::uint64_t visits = 0;
    for (std::size_t i = 0; i < inserted.size(); ++i) {
        if ((i & 255) == 0)
            terrainCheckpoint(progress, TerrainStage::Replay, 0);
        const auto q = v2(inserted[i]);
        const auto start = containing(q);
        visits += start.size();
        std::set<TerrainTriangleRef> visited(start.begin(), start.end());
        std::vector<TerrainTriangleRef> walk(start.begin(), start.end());
        for (std::size_t head = 0; head < walk.size(); ++head) {
            const auto ref = walk[head];
            const auto c = view.corners(ref);
            for (int k = 0; k < 3; ++k) {
                const auto &a = c[std::size_t(k)], &b = c[std::size_t((k + 1) % 3)];
                if (constraint(a, b))
                    continue;
                const auto next = view.across(ref, a, b);
                if (!next || visited.contains(*next))
                    continue;
                visited.insert(*next);
                if (++visits > terrainSourceEditTouchedTriangles)
                    problems.refuse(limitKind,
                                    "The pad would re-triangulate too much of the terrain; use a "
                                    "smaller footprint or maximum distance.",
                                    q);
                const auto t = view.corners(*next);
                if (terrainInCircle(v2(t[0]), v2(t[1]), v2(t[2]), q) <= 0)
                    continue;
                if (!pads.empty() && pads.covers((t[0].x + t[1].x + t[2].x) / 3,
                                                 (t[0].y + t[1].y + t[2].y) / 3))
                    continue;
                grow(*next, q);
                walk.push_back(*next);
            }
        }
    }
    queue.clear();
    // ---- Current sources: clipped by the widened polygon ----
    TerrainSourceClipRule rule;
    rule.box = {polygon.box()[0] - slack, polygon.box()[1] - slack, polygon.box()[2] + slack,
                polygon.box()[3] + slack};
    rule.vertex = [&](const Vec3 &p) {
        classifyCost(v2(p));
        return polygon.near(p.x, p.y, slack);
    };
    rule.segment = [&](const Vec3 &a, const Vec3 &b) {
        classifyCost(v2(a));
        return polygon.segmentNear(v2(a), v2(b), slack);
    };
    rule.firstOrdinal = 2;
    rule.tooManyPieces = "This pad would split source lines into more than 4,094 pieces; use a "
                         "smaller footprint or maximum distance";
    plan.clip = planTerrainSourceClip(state, edit.id, data.pieces, rule, progress);
    if (!plan.clip.refusal.empty())
        problems.refuse(limitKind, plan.clip.refusal, v2(footprint.front()));
    resolved->pieces = plan.clip.resolved;
    // ---- The adapter's input ----
    TerrainCavityInput input;
    std::map<TerrainPlanKey, std::uint32_t> index;
    std::vector<Vec3> positions;
    constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    for (const auto &ref : cavity) {
        const auto c = view.corners(ref);
        std::array<std::uint32_t, 3> tri{};
        for (int i = 0; i < 3; ++i) {
            const auto &p = c[std::size_t(i)];
            const auto key = TerrainPlanKey::of(p.x, p.y);
            if (removedVertices.contains(key)) {
                tri[std::size_t(i)] = none;
                continue;
            }
            const auto [it, fresh] = index.try_emplace(key, std::uint32_t(positions.size()));
            if (fresh) {
                positions.push_back(p);
                input.vertices.push_back({{p.x, p.y, p.z}, state.contourUses(key) > 0, 0});
            }
            tri[std::size_t(i)] = it->second;
        }
        for (int i = 0; i < 3; ++i)
            directed[{tri[std::size_t(i)], tri[std::size_t((i + 1) % 3)]}] += 1;
    }
    for (const auto &[e, uses] : directed) {
        const auto [a, b] = e;
        if (directed.contains({b, a})) {
            if (a < b && a != none && b != none && constraint(positions[a], positions[b]))
                input.constraints.push_back({a, b});
            continue;
        }
        if (a == none || b == none)
            problems.refuse("triangulation-failed",
                            "A removed vertex lies on the pad's cavity boundary.",
                            v2(footprint.front()));
        input.boundary.push_back({a, b});
    }
    {
        TerrainBreakline pad;
        pad.closed = true;
        pad.sourceId = *outlineSource;
        for (const auto &p : outline)
            pad.points.push_back({p[0], p[1], h});
        input.breaklines.push_back(std::move(pad));
        for (const auto &ray : resolved->rays) {
            const auto &s = outline[ray.start];
            if (ray.daylight.x == s[0] && ray.daylight.y == s[1])
                continue;
            input.breaklines.push_back(
                {{{s[0], s[1], h}, {ray.daylight.x, ray.daylight.y, ray.daylight.z}},
                 false,
                 *daylightSource});
        }
        TerrainBreakline line;
        line.closed = true;
        line.sourceId = *daylightSource;
        for (const auto &p : ring)
            line.points.push_back({p.x, p.y, p.z});
        input.breaklines.push_back(std::move(line));
    }
    TerrainTriangulationOptions options;
    options.flatMode = context.parameters.flipFlatTriangles ? TerrainFlatMode::Flip
                                                            : TerrainFlatMode::ReportOnly;
    terrainCheckpoint(progress, TerrainStage::Replay, 0);
    const auto result = retriangulateTerrainCavity(input, options);
    if (!result.ok) {
        for (const auto &problem : result.report.refusals)
            problems.merge(problem);
        problems.refuse();
    }
    auto vertex = [&](std::uint32_t i) {
        if (i < positions.size())
            return positions[i];
        const auto &v = result.vertices[i];
        return Vec3{v.x, v.y, v.z};
    };
    // Every new point is where the entry stores it (C1, C3): one the adapter merged into another
    // within 1e-7 m is refused.
    {
        std::size_t k = 0;
        for (const auto &line : input.breaklines)
            for (const auto &p : line.points) {
                if (k < result.placed.size()) {
                    const auto at = vertex(result.placed[k]);
                    if (at.x != p.x || at.y != p.y || at.z != p.z)
                        problems.add("conflicting-duplicate",
                                     "Two pad points lie within 1e-7 m of each other; simplify "
                                     "the footprint.",
                                     {p.x, p.y});
                }
                ++k;
            }
    }
    for (const auto &t : result.triangles) {
        const std::array<Vec3, 3> c{vertex(t[0]), vertex(t[1]), vertex(t[2])};
        if (length(cross(c[1] - c[0], c[2] - c[0])) < 1e-12)
            problems.add("degenerate-triangle",
                         "Triangles below the modelling tolerance (area under 5e-13 m²) cannot "
                         "become faces.",
                         {(c[0].x + c[1].x + c[2].x) / 3, (c[0].y + c[1].y + c[2].y) / 3});
        plan.change.added.push_back(c);
    }
    problems.check();
    plan.change.removed.assign(cavity.begin(), cavity.end());
    plan.cavityTriangles = cavity.size();
    plan.addedTriangles = plan.change.added.size();
    plan.removedVertices = removedVertices.size();
    plan.flatRemaining = result.report.flatTrianglesRemaining;
    plan.resolved = std::move(resolved);
    return plan;
}

// ---- The kind -------------------------------------------------------------------------------
namespace {
// The current sources after a resolved stamp: the clip by its widened daylight polygon, then its
// outline and daylight line as closed breaklines.
void applyStampSources(TerrainSourceState &state, const TerrainEdit &edit) {
    const auto &data = dataOf(edit);
    if (!data.resolved())
        return;
    const auto ring = terrainStampRing(data);
    std::vector<Plan2> ringPlan;
    for (const auto &p : ring)
        ringPlan.push_back({p.x, p.y});
    const TerrainPolygonIndex polygon(ringPlan);
    TerrainSourceClipRule rule;
    constexpr double slack = terrainStampSlack;
    rule.box = {polygon.box()[0] - slack, polygon.box()[1] - slack, polygon.box()[2] + slack,
                polygon.box()[3] + slack};
    rule.vertex = [&](const Vec3 &p) { return polygon.near(p.x, p.y, slack); };
    rule.segment = [&](const Vec3 &a, const Vec3 &b) {
        return polygon.segmentNear(v2(a), v2(b), slack);
    };
    rule.firstOrdinal = 2;
    applyTerrainSculptClip(state, planTerrainSourceClip(state, edit.id, data.pieces, rule));
    // Older pads' rays the widened polygon meets are superseded; this pad's rays join.
    state.dropPadRays(rule.box, rule.segment);
    for (const auto &ray : data.rays) {
        const auto &s = data.outline[ray.start];
        if (!(ray.daylight.x == s[0] && ray.daylight.y == s[1]))
            state.addPadRay(edit.id, {s[0], s[1], data.elevation}, ray.daylight);
    }
    const auto outlineId = terrainStampOutlineSource(edit.id),
               daylightId = terrainStampDaylightSource(edit.id);
    if (!outlineId || !daylightId)
        return;
    TerrainSource pad;
    pad.id = *outlineId;
    pad.role = TerrainSourceRole::Breakline;
    pad.reference = "pad " + std::to_string(edit.id);
    for (const auto &p : data.outline)
        pad.points.push_back({p[0], p[1], data.elevation});
    pad.points.push_back(pad.points.front());
    TerrainSource line;
    line.id = *daylightId;
    line.role = TerrainSourceRole::Breakline;
    line.reference = "pad " + std::to_string(edit.id) + " daylight";
    line.points = ring;
    line.points.push_back(line.points.front());
    for (auto *source : {&pad, &line})
        if (!state.find(source->id)) {
            state.markPadSource(source->id, edit.id);
            state.add(std::move(*source));
        }
}
} // namespace
std::vector<TerrainEditKind> terrainStampKinds() {
    TerrainEditKind kind;
    kind.name = terrainStampKind;
    kind.applyInContext = [](const TerrainEditContext &context, const TerrainEdit &edit) {
        auto plan = planTerrainStamp(context, edit);
        return TerrainEditOutcome{std::move(plan.change), {}, std::move(plan.resolved)};
    };
    kind.sources = applyStampSources;
    kind.bounds = [](const TerrainEdit &edit) { return terrainStampBounds(dataOf(edit)); };
    kind.protects = [](const TerrainEdit &edit) {
        const auto &data = dataOf(edit);
        return data.resolved() ? std::vector<TerrainPlanPolygon>{data.footprint}
                               : std::vector<TerrainPlanPolygon>{};
    };
    kind.protectedPoints = [](const TerrainEdit &edit) {
        const auto &data = dataOf(edit);
        return data.resolved() ? data.outline : std::vector<Plan2>{};
    };
    kind.unresolved = [](const TerrainEdit &edit) -> std::shared_ptr<const TerrainEditData> {
        const auto &data = dataOf(edit);
        if (!data.resolved())
            return nullptr;
        auto cleared = std::make_shared<TerrainStampData>(data);
        cleared->outline.clear();
        cleared->rays.clear();
        cleared->dropped = 0;
        return cleared;
    };
    return {std::move(kind)};
}

// ---- Check C3 -------------------------------------------------------------------------------
TerrainPadConsistency checkTerrainPadConsistency(const Document &doc, Id terrain,
                                                 const std::vector<std::uint32_t> &skipped) {
    if (!isTerrain(doc, terrain))
        throw TerrainError("NOT_FOUND", "Terrain body does not exist");
    TerrainPadConsistency report;
    const auto &record = *doc.bodies().at(terrain)->terrain;
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    auto problem = [&](std::string text) {
        if (report.problems.size() < 32)
            report.problems.push_back(std::move(text));
    };
    auto where = [](double x, double y) {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    };
    auto isSkipped = [&](std::uint32_t id) {
        return std::find(skipped.begin(), skipped.end(), id) != skipped.end();
    };
    auto edge = [&](const Vec3 &a, const Vec3 &b) {
        const auto l = mesh->trianglesAt(a), r = mesh->trianglesAt(b);
        std::vector<TerrainTriangleRef> shared;
        std::set_intersection(l.begin(), l.end(), r.begin(), r.end(), std::back_inserter(shared));
        return !shared.empty();
    };
    const auto &edits = record.edits;
    // Each applied pad's widened daylight polygon, built once (later pads supersede rays).
    std::vector<std::shared_ptr<TerrainPolygonIndex>> rings(edits.size());
    for (std::size_t f = 0; f < edits.size(); ++f)
        if (edits[f]->kind == terrainStampKind && !isSkipped(edits[f]->id) &&
            dataOf(*edits[f]).resolved()) {
            std::vector<Plan2> ring;
            for (const auto &p : terrainStampRing(dataOf(*edits[f])))
                ring.push_back({p.x, p.y});
            rings[f] = std::make_shared<TerrainPolygonIndex>(std::move(ring));
        }
    for (std::size_t e = 0; e < edits.size(); ++e) {
        const auto &entry = *edits[e];
        if (entry.kind != terrainStampKind || isSkipped(entry.id))
            continue;
        const auto &data = dataOf(entry);
        if (!data.resolved())
            continue;
        ++report.pads;
        const double h = data.elevation;
        const auto region = terrainProtectedRegion(entry.id, data.footprint, data.outline);
        // Pad vertices.
        for (const auto *tile : mesh->tilesIn(region.box[0], region.box[1], region.box[2],
                                              region.box[3], 0))
            for (const auto &p : tile->content->vertices) {
                const bool covered =
                    region.index->contains(p.x, p.y) ||
                    std::binary_search(region.keys->begin(), region.keys->end(),
                                       TerrainPlanKey::of(p.x, p.y));
                if (!covered)
                    continue;
                ++report.padVertices;
                if (p.z != h) {
                    ++report.wrongElevations;
                    problem("pad " + std::to_string(entry.id) + " vertex " + where(p.x, p.y) +
                            " has Z " + std::to_string(p.z) + ", not " + std::to_string(h));
                }
            }
        // Outline edges.
        const auto m = data.outline.size();
        for (std::size_t i = 0; i < m; ++i) {
            const auto &a = data.outline[i], &b = data.outline[(i + 1) % m];
            ++report.outlineEdges;
            if (!edge({a[0], a[1], h}, {b[0], b[1], h})) {
                ++report.missingEdges;
                problem("pad " + std::to_string(entry.id) + " outline edge from " +
                        where(a[0], a[1]) + " is not a mesh edge");
            }
        }
        // Later entries that may supersede rays.
        std::vector<std::pair<const TerrainEdit *, std::shared_ptr<TerrainPolygonIndex>>> later;
        for (std::size_t f = e + 1; f < edits.size(); ++f) {
            if (isSkipped(edits[f]->id) || (edits[f]->kind == terrainStampKind && !rings[f]))
                continue;
            later.push_back({edits[f].get(), rings[f]});
        }
        for (const auto &ray : data.rays) {
            const auto &s = data.outline[ray.start];
            const Vec3 start{s[0], s[1], h};
            if (ray.daylight.x == s[0] && ray.daylight.y == s[1])
                continue;
            ++report.rays;
            Box box = emptyBox();
            extend(box, s[0], s[1]);
            extend(box, ray.daylight.x, ray.daylight.y);
            bool superseded = false;
            for (const auto &[other, polygon] : later) {
                if (polygon)
                    superseded = polygon->segmentNear({s[0], s[1]}, v2(ray.daylight),
                                                      terrainStampSlack);
                else if (const auto *kind = findTerrainEditKind(other->kind); kind && kind->bounds)
                    superseded = boxesMeet(box, kind->bounds(*other), terrainStampSlack);
                if (superseded)
                    break;
            }
            if (superseded) {
                ++report.superseded;
                continue;
            }
            const bool isEdge = edge(start, ray.daylight);
            const auto at = mesh->trianglesAt(ray.daylight);
            const double run = planDistance({s[0], s[1]}, v2(ray.daylight));
            bool grade = true;
            // Within 1e-9 relative, plus the rounding of the stored point: a few units in the
            // last place of the coordinates and elevations, over the run.
            constexpr double ulp = 0x1p-52;
            const double scale = std::max({std::abs(s[0]), std::abs(s[1]), std::abs(ray.daylight.x),
                                           std::abs(ray.daylight.y)});
            const double zScale = std::max(std::abs(h), std::abs(ray.daylight.z));
            if (data.mode == TerrainStampMode::Daylight) {
                const double rise = std::abs(h - ray.daylight.z);
                const double slope = ray.daylight.z < h ? data.fillSlope : data.cutSlope;
                const double tolerance = 1e-9 * slope + 8 * ulp * (zScale + slope * scale) / run;
                grade = ray.daylight.z != h && std::abs(rise / run - slope) <= tolerance;
            } else
                grade = std::abs(run - data.width) <= 1e-9 * data.width + 8 * ulp * scale;
            if (!isEdge || at.empty() || !grade) {
                ++report.wrongRays;
                problem("pad " + std::to_string(entry.id) + " ray from " + where(s[0], s[1]) +
                        (!isEdge      ? " is not a mesh edge"
                         : at.empty() ? " ends off the mesh"
                                      : " does not keep its grade"));
            }
        }
    }
    return report;
}
} // namespace sketchy
