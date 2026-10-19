// R095.b (ADR 0168 section 3). See terrain_triangulation.hpp for the contract and the
// implementation note in the ADR for the method. Pipeline:
//
//   1. Validate numbers and sizes. 2. Merge vertices (contours, breaklines, points, then
//   boundary and hole vertices). 3. Drop sources outside the boundary or inside a hole.
//   4. Find crossing, touching and overlapping constraint segments with exact predicates.
//   5. Elevations for boundary and hole vertices from a first triangulation of the sources.
//   6. CDT: vertices, constraint edges, erase the super triangle.
//   7. Remove triangles outside the boundary and inside holes (depth by region edges), trim
//      by maximum edge length. 8. Flip flat triangles, optionally fill with Steiner points.
//   9. Compact vertices, sort triangles, report.
//
// The library is used for constrained Delaunay only. Everything that decides what the
// caller sees (refusals, region removal, flats) is here, on exact predicates, so a library
// upgrade changes triangle choice at co-circular ties and nothing else.
#include "geometry/terrain_triangulation.hpp"
// D-044: CDT's upstream abort hook (ICallbackHandler::isAbortCalculation, polled per vertex and
// per constraint edge). The switch only compiles the hook in; the vendored headers are
// unchanged, and only this file includes them, so the switch is the same for every use.
#define CDT_ENABLE_CALLBACK_HANDLER
#include <CDT/CDT.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <queue>
#include <unordered_map>

namespace sketchy {
namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr u32 kNone = std::numeric_limits<u32>::max();
constexpr std::size_t kMaxCrossingPairs = 1000000; // pairs counted before the count stops growing
constexpr std::size_t kMaxGridEntries = 64000000;  // segment-cell registrations

struct Pt {
    double x{0};
    double y{0};
};

int orientSign(const Pt &a, const Pt &b, const Pt &c) {
    const double d = predicates::orient2d<double>(a.x, a.y, b.x, b.y, c.x, c.y);
    return (d > 0) - (d < 0);
}
double orientValue(const Pt &a, const Pt &b, const Pt &c) {
    return predicates::orient2d<double>(a.x, a.y, b.x, b.y, c.x, c.y);
}
bool inBox(const Pt &a, const Pt &b, const Pt &p) {
    return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
           p.y <= std::max(a.y, b.y);
}
u64 edgeKey(u32 a, u32 b) { return a < b ? (u64(a) << 32 | b) : (u64(b) << 32 | a); }

// ---------------------------------------------------------------------------------------
// Problems: counts are complete, locations and messages are bounded.
// ---------------------------------------------------------------------------------------
class Problems {
  public:
    void add(TerrainProblemKind kind, const TerrainLocation *where = nullptr) {
        auto &entry = entries_[std::size_t(kind)];
        ++entry.count;
        if (where && entry.locations.size() < terrainMaxLocationsPerKind)
            entry.locations.push_back(*where);
    }
    void add(TerrainProblemKind kind, Pt xy, u64 a = 0, u64 b = 0) {
        const TerrainLocation where{{xy.x, xy.y}, a, b};
        add(kind, &where);
    }
    void setCount(TerrainProblemKind kind, u64 count) { entries_[std::size_t(kind)].count = count; }
    void detail(TerrainProblemKind kind, std::string text) {
        entries_[std::size_t(kind)].detail = std::move(text);
    }
    bool any() const {
        for (const auto &entry : entries_)
            if (entry.count)
                return true;
        return false;
    }
    bool has(TerrainProblemKind kind) const { return entries_[std::size_t(kind)].count != 0; }
    u64 count(TerrainProblemKind kind) const { return entries_[std::size_t(kind)].count; }
    std::vector<TerrainProblem> finish() const {
        std::vector<TerrainProblem> out;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto &entry = entries_[i];
            if (!entry.count)
                continue;
            TerrainProblem problem;
            problem.kind = TerrainProblemKind(i);
            problem.count = entry.count;
            problem.locations = entry.locations;
            problem.message = message(problem.kind, entry);
            out.push_back(std::move(problem));
        }
        return out;
    }

  private:
    struct Entry {
        u64 count{0};
        std::vector<TerrainLocation> locations;
        std::string detail;
    };
    static std::string message(TerrainProblemKind kind, const Entry &entry) {
        char text[200];
        const unsigned long long n = entry.count;
        switch (kind) {
        case TerrainProblemKind::NonFinite:
            std::snprintf(text, sizeof text, "%llu coordinate(s) are not finite numbers.", n);
            break;
        case TerrainProblemKind::ExtentExceeded:
            std::snprintf(text, sizeof text,
                          "%llu coordinate(s) are beyond the local frame bound; convert to local "
                          "coordinates first.",
                          n);
            break;
        case TerrainProblemKind::LimitExceeded:
            std::snprintf(text, sizeof text, "An input limit was exceeded (%llu).", n);
            break;
        case TerrainProblemKind::ConflictingDuplicate:
            std::snprintf(text, sizeof text,
                          "%llu vertex pair(s) share an x and y but have different elevations.", n);
            break;
        case TerrainProblemKind::CrossingConstraints:
            std::snprintf(text, sizeof text,
                          "%llu constraint segment pair(s) cross, touch or overlap.", n);
            break;
        case TerrainProblemKind::InsufficientPoints:
            std::snprintf(text, sizeof text, "Fewer than three non-collinear points (%llu usable).",
                          n);
            break;
        case TerrainProblemKind::InvalidPolygon:
            std::snprintf(text, sizeof text, "%llu boundary or hole polygon(s) are invalid.", n);
            break;
        case TerrainProblemKind::TriangleBudget:
            std::snprintf(text, sizeof text, "The triangulation has too many triangles.");
            break;
        case TerrainProblemKind::TriangulationFailed:
            std::snprintf(text, sizeof text, "The triangulation library failed.");
            break;
        }
        std::string out = entry.detail.empty() ? std::string(text) : entry.detail;
        if (out.size() > terrainMaxMessageBytes)
            out.resize(terrainMaxMessageBytes);
        return out;
    }
    std::array<Entry, terrainProblemKindCount> entries_{};
};

std::string format(const char *pattern, unsigned long long a, unsigned long long b = 0) {
    char text[terrainMaxMessageBytes + 1];
    std::snprintf(text, sizeof text, pattern, a, b);
    return text;
}

// ---------------------------------------------------------------------------------------
// Point in polygon with exact predicates; edges are bucketed into horizontal slabs so a
// query sees a few edges, not all of them.
// ---------------------------------------------------------------------------------------
class PolygonIndex {
  public:
    enum class Where { Outside, Boundary, Inside };
    explicit PolygonIndex(const std::vector<Pt> &points) : p_(points) {
        const std::size_t n = p_.size();
        xmin_ = ymin_ = std::numeric_limits<double>::max();
        xmax_ = ymax_ = std::numeric_limits<double>::lowest();
        for (const auto &q : p_) {
            xmin_ = std::min(xmin_, q.x);
            xmax_ = std::max(xmax_, q.x);
            ymin_ = std::min(ymin_, q.y);
            ymax_ = std::max(ymax_, q.y);
        }
        slabs_ = std::clamp<std::size_t>(n, 1, 512);
        start_.assign(slabs_ + 1, 0);
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<u32> fill(slabs_, 0);
            for (std::size_t i = 0; i < n; ++i) {
                const auto &a = p_[i];
                const auto &b = p_[(i + 1) % n];
                for (std::size_t s = slab(std::min(a.y, b.y)); s <= slab(std::max(a.y, b.y)); ++s) {
                    if (pass == 0)
                        ++start_[s + 1];
                    else
                        edges_[start_[s] + fill[s]++] = u32(i);
                }
            }
            if (pass == 0) {
                for (std::size_t s = 0; s < slabs_; ++s)
                    start_[s + 1] += start_[s];
                edges_.resize(start_[slabs_]);
            }
        }
    }
    Where locate(const Pt &q) const {
        if (q.x < xmin_ || q.x > xmax_ || q.y < ymin_ || q.y > ymax_)
            return Where::Outside;
        const std::size_t n = p_.size();
        const std::size_t s = slab(q.y);
        int winding = 0;
        for (u32 k = start_[s]; k < start_[s + 1]; ++k) {
            const auto &a = p_[edges_[k]];
            const auto &b = p_[(edges_[k] + 1) % n];
            const double o = orientValue(a, b, q);
            if (o == 0 && inBox(a, b, q))
                return Where::Boundary;
            if (a.y <= q.y) {
                if (b.y > q.y && o > 0)
                    ++winding;
            } else if (b.y <= q.y && o < 0) {
                --winding;
            }
        }
        return winding != 0 ? Where::Inside : Where::Outside;
    }

  private:
    std::size_t slab(double y) const {
        if (!(ymax_ > ymin_))
            return 0;
        const double t = (y - ymin_) / (ymax_ - ymin_) * double(slabs_);
        return t <= 0 ? 0 : std::min(slabs_ - 1, std::size_t(t));
    }
    const std::vector<Pt> &p_;
    double xmin_, xmax_, ymin_, ymax_;
    std::size_t slabs_;
    std::vector<u32> start_;
    std::vector<u32> edges_;
};

// ---------------------------------------------------------------------------------------
// Vertex store with tolerance merging through a hash of cells one tolerance wide.
// ---------------------------------------------------------------------------------------
enum VertexFlag : unsigned char { HasZ = 1, OnContour = 2, OnPolygon = 4, Dropped = 8 };

struct CellKey {
    long long x, y;
    bool operator==(const CellKey &) const = default;
};
struct CellHash {
    std::size_t operator()(const CellKey &k) const {
        return std::size_t(u64(k.x) * 0x9E3779B97F4A7C15ull ^
                           (u64(k.y) + 0x7F4A7C15ull) * 0xC2B2AE3D27D4EB4Full);
    }
};

class VertexStore {
  public:
    VertexStore(double xyTol, double zTol)
        : tol_(xyTol), zTol_(zTol), cell_(xyTol > 0 ? xyTol : 1e-12) {}
    struct Added {
        u32 id;
        bool merged;
        bool conflict;
        u32 other;
    };
    Added add(double x, double y, double zValue, bool hasZ, bool contour, u64 source) {
        const long long cx = (long long)std::floor(x / cell_);
        const long long cy = (long long)std::floor(y / cell_);
        u32 best = kNone;
        for (long long j = cy - 1; j <= cy + 1; ++j)
            for (long long i = cx - 1; i <= cx + 1; ++i) {
                const auto it = cells_.find({i, j});
                if (it == cells_.end())
                    continue;
                for (u32 id : it->second)
                    if (std::abs(xy[id].x - x) <= tol_ && std::abs(xy[id].y - y) <= tol_ &&
                        id < best)
                        best = id;
            }
        if (best != kNone) {
            bool conflict = false;
            if (hasZ && (flags[best] & HasZ)) {
                conflict = std::abs(z[best] - zValue) > zTol_;
                if (!conflict && contour)
                    flags[best] |= OnContour;
            } else if (hasZ) {
                z[best] = zValue;
                flags[best] |= HasZ | (contour ? OnContour : 0);
            }
            return {best, true, conflict, best};
        }
        const u32 id = u32(xy.size());
        xy.push_back({x, y});
        z.push_back(hasZ ? zValue : 0.0);
        flags.push_back(
            static_cast<unsigned char>((hasZ ? HasZ : 0) | (contour && hasZ ? OnContour : 0)));
        source_.push_back(source);
        cells_[{cx, cy}].push_back(id);
        return {id, false, false, id};
    }
    u64 source(u32 id) const { return source_[id]; }
    std::vector<Pt> xy;
    std::vector<double> z;
    std::vector<unsigned char> flags;

  private:
    double tol_, zTol_, cell_;
    std::vector<u64> source_;
    std::unordered_map<CellKey, std::vector<u32>, CellHash> cells_;
};

// ---------------------------------------------------------------------------------------
// Constraint segments.
// ---------------------------------------------------------------------------------------
enum SegmentKind : unsigned char { DataSegment = 1, RegionSegment = 2 };
struct Segment {
    u32 a, b;
    unsigned char kind;
    u64 source;
};

// Merge repeated segments. Two region segments on the same edge are an error: the depth
// rule counts each region edge once.
void dedupe(std::vector<Segment> &segments, const std::vector<Pt> &xy, Problems &problems,
            TerrainTriangulationReport &report) {
    std::unordered_map<u64, std::size_t> seen;
    std::vector<Segment> out;
    out.reserve(segments.size());
    for (const auto &s : segments) {
        const auto [it, fresh] = seen.try_emplace(edgeKey(s.a, s.b), out.size());
        if (fresh) {
            out.push_back(s);
            continue;
        }
        auto &kept = out[it->second];
        if ((kept.kind & RegionSegment) && (s.kind & RegionSegment)) {
            problems.add(TerrainProblemKind::CrossingConstraints,
                         Pt{(xy[s.a].x + xy[s.b].x) / 2, (xy[s.a].y + xy[s.b].y) / 2}, kept.source,
                         s.source);
            continue;
        }
        kept.kind |= s.kind;
        ++report.mergedSegments;
    }
    segments = std::move(out);
}

// Uniform grid over the kept vertices, used for segment-vertex and segment-segment tests.
class Grid {
  public:
    Grid(const std::vector<Pt> &xy, const std::vector<unsigned char> &flags, std::size_t items) {
        double xmax = std::numeric_limits<double>::lowest(), ymax = xmax;
        x0_ = y0_ = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < xy.size(); ++i) {
            if (flags[i] & Dropped)
                continue;
            x0_ = std::min(x0_, xy[i].x);
            xmax = std::max(xmax, xy[i].x);
            y0_ = std::min(y0_, xy[i].y);
            ymax = std::max(ymax, xy[i].y);
        }
        if (x0_ > xmax) {
            x0_ = y0_ = xmax = ymax = 0;
        }
        n_ = int(std::clamp<double>(std::ceil(std::sqrt(double(items) / 2.0)), 1.0, 1024.0));
        cw_ = (xmax - x0_) > 0 ? (xmax - x0_) / n_ : 1.0;
        ch_ = (ymax - y0_) > 0 ? (ymax - y0_) / n_ : 1.0;
    }
    int cells() const { return n_ * n_; }
    int cx(double x) const { return std::clamp(int(std::floor((x - x0_) / cw_)), 0, n_ - 1); }
    int cy(double y) const { return std::clamp(int(std::floor((y - y0_) / ch_)), 0, n_ - 1); }
    int cell(const Pt &p) const { return cy(p.y) * n_ + cx(p.x); }
    // Every cell the segment passes through, plus a one-cell margin against rounding.
    template <class F> void raster(const Pt &a, const Pt &b, F &&visit) const {
        const double ylo = std::min(a.y, b.y), yhi = std::max(a.y, b.y);
        const double sxlo = std::min(a.x, b.x), sxhi = std::max(a.x, b.x);
        for (int r = cy(ylo); r <= cy(yhi); ++r) {
            const double rowLo = y0_ + r * ch_;
            const double u = std::max(ylo, rowLo), v = std::min(yhi, rowLo + ch_);
            double xl, xh;
            if (a.y == b.y) {
                xl = sxlo;
                xh = sxhi;
            } else {
                const auto at = [&](double y) {
                    return a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
                };
                xl = std::min(at(u), at(v));
                xh = std::max(at(u), at(v));
                xl = std::clamp(xl, sxlo, sxhi);
                xh = std::clamp(xh, sxlo, sxhi);
            }
            const int c0 = std::max(0, cx(xl) - 1), c1 = std::min(n_ - 1, cx(xh) + 1);
            for (int c = c0; c <= c1; ++c)
                visit(r * n_ + c);
        }
    }

  private:
    double x0_, y0_, cw_, ch_;
    int n_;
};

// CSR lists of items per cell.
struct CellLists {
    std::vector<u32> start, items;
};

// ---------------------------------------------------------------------------------------
// Triangle mesh with neighbours. Triangle t has vertices v[t][0..2] counter-clockwise and
// nb[t][i] is the triangle across the edge (v[i], v[i+1]), or kNone on the outer edge.
// ---------------------------------------------------------------------------------------
using Tri = std::array<u32, 3>;
struct Mesh {
    std::vector<Tri> v, nb;
};

bool buildNeighbors(Mesh &m) {
    struct Rec {
        u64 key;
        u32 t, slot;
    };
    std::vector<Rec> recs;
    recs.reserve(m.v.size() * 3);
    for (u32 t = 0; t < m.v.size(); ++t)
        for (u32 i = 0; i < 3; ++i)
            recs.push_back({edgeKey(m.v[t][i], m.v[t][(i + 1) % 3]), t, i});
    std::sort(recs.begin(), recs.end(), [](const Rec &l, const Rec &r) {
        return l.key != r.key ? l.key < r.key : l.t != r.t ? l.t < r.t : l.slot < r.slot;
    });
    m.nb.assign(m.v.size(), Tri{kNone, kNone, kNone});
    for (std::size_t i = 0; i < recs.size();) {
        std::size_t j = i + 1;
        while (j < recs.size() && recs[j].key == recs[i].key)
            ++j;
        if (j - i > 2)
            return false; // non-manifold: cannot happen from a valid triangulation
        if (j - i == 2) {
            m.nb[recs[i].t][recs[i].slot] = recs[i + 1].t;
            m.nb[recs[i + 1].t][recs[i + 1].slot] = recs[i].t;
        }
        i = j;
    }
    return true;
}

// Replace the neighbour across edge {u, w} in `t` (found by its vertices) with `to`.
void relink(Mesh &m, u32 t, u32 u, u32 w, u32 to) {
    if (t == kNone)
        return;
    for (u32 i = 0; i < 3; ++i)
        if ((m.v[t][i] == u && m.v[t][(i + 1) % 3] == w) ||
            (m.v[t][i] == w && m.v[t][(i + 1) % 3] == u))
            m.nb[t][i] = to;
}

bool sortedContains(const std::vector<u64> &keys, u64 key) {
    return std::binary_search(keys.begin(), keys.end(), key);
}

// Everything the flat pass needs about vertices. Steiner points are appended.
struct Surface {
    std::vector<Pt> xy;
    std::vector<double> z;
    std::vector<unsigned char> contour;
};

bool isFlat(const Surface &s, const Tri &t, double tol) {
    return s.contour[t[0]] && s.contour[t[1]] && s.contour[t[2]] &&
           std::abs(s.z[t[0]] - s.z[t[1]]) <= tol && std::abs(s.z[t[0]] - s.z[t[2]]) <= tol;
}
double planArea(const Surface &s, const Tri &t) {
    return std::abs(orientValue(s.xy[t[0]], s.xy[t[1]], s.xy[t[2]])) / 2;
}

// Flip an edge when the two triangles form a strictly convex quadrilateral. Triangle t is
// (a, b, c) and its neighbour across slot-0 edge is (b, a, d). Returns the new pair.
bool flipEdge(Mesh &m, const Surface &s, u32 t, u32 slot, double flatTol, bool requireFewerFlats,
              bool &flipped) {
    flipped = false;
    const u32 n = m.nb[t][slot];
    if (n == kNone)
        return true;
    const u32 a = m.v[t][slot], b = m.v[t][(slot + 1) % 3], c = m.v[t][(slot + 2) % 3];
    u32 d = kNone;
    for (u32 i = 0; i < 3; ++i)
        if (m.v[n][i] != a && m.v[n][i] != b)
            d = m.v[n][i];
    if (d == kNone)
        return false;
    if (!(orientSign(s.xy[c], s.xy[a], s.xy[d]) > 0 && orientSign(s.xy[c], s.xy[d], s.xy[b]) > 0))
        return true;
    const Tri first{c, a, d}, second{c, d, b};
    if (requireFewerFlats) {
        const int before = int(isFlat(s, m.v[t], flatTol)) + int(isFlat(s, m.v[n], flatTol));
        const int after = int(isFlat(s, first, flatTol)) + int(isFlat(s, second, flatTol));
        if (after >= before)
            return true;
    }
    // Outer neighbours, found before any edit.
    const auto across = [&](u32 tri, u32 p, u32 q) {
        for (u32 i = 0; i < 3; ++i)
            if (m.v[tri][i] == p && m.v[tri][(i + 1) % 3] == q)
                return m.nb[tri][i];
        return kNone;
    };
    const u32 nbBC = across(t, b, c), nbCA = across(t, c, a), nbAD = across(n, a, d),
              nbDB = across(n, d, b);
    m.v[t] = first;
    m.v[n] = second;
    m.nb[t] = {nbCA, nbAD, n};
    m.nb[n] = {t, nbDB, nbBC};
    relink(m, nbCA, c, a, t);
    relink(m, nbAD, a, d, t);
    relink(m, nbDB, d, b, n);
    relink(m, nbBC, b, c, n);
    flipped = true;
    return true;
}

} // namespace

int terrainOrientation(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c) {
    return orientSign(Pt{a.x, a.y}, Pt{b.x, b.y}, Pt{c.x, c.y});
}

const char *terrainProblemKindName(TerrainProblemKind kind) {
    switch (kind) {
    case TerrainProblemKind::NonFinite:
        return "non-finite";
    case TerrainProblemKind::ExtentExceeded:
        return "extent-exceeded";
    case TerrainProblemKind::LimitExceeded:
        return "limit-exceeded";
    case TerrainProblemKind::ConflictingDuplicate:
        return "conflicting-duplicate";
    case TerrainProblemKind::CrossingConstraints:
        return "crossing-constraints";
    case TerrainProblemKind::InsufficientPoints:
        return "insufficient-points";
    case TerrainProblemKind::InvalidPolygon:
        return "invalid-polygon";
    case TerrainProblemKind::TriangleBudget:
        return "triangle-budget";
    case TerrainProblemKind::TriangulationFailed:
        return "triangulation-failed";
    }
    return "unknown";
}

namespace {

using CdtTriangulation = CDT::Triangulation<double>;

// Triangulate-stage checkpoints. `step(f)` reports f (0..1 of the stage) and checks for
// cancellation; `every(i, n, from, to)` does so once per 16,384 iterations of a loop.
struct Steps {
    TerrainProgress *progress;
    void operator()(double fraction) const {
        terrainCheckpoint(progress, TerrainStage::Triangulate, fraction);
    }
    void every(std::size_t i, std::size_t n, double from, double to) const {
        if (progress && (i & 0x3fff) == 0)
            (*this)(from + (to - from) * double(i) / double(std::max<std::size_t>(n, 1)));
    }
};

// CDT's abort hook: polled once per inserted vertex or constraint edge; every 256th poll is a
// checkpoint, with the fraction of the phase's items done. An abort is latched, as CDT checks
// the hook again after it stops; the caller then throws TerrainCancelled.
class CdtCheck final : public CDT::ICallbackHandler {
  public:
    explicit CdtCheck(Steps steps) : steps_(steps) {}
    void phase(double from, double to, std::size_t items) {
        from_ = from;
        to_ = to;
        items_ = std::max<std::size_t>(items, 1);
        polls_ = 0;
    }
    bool isAbortCalculation() const override {
        if (aborted_)
            return true;
        if ((++polls_ & 0xff) != 0)
            return false;
        try {
            steps_(from_ + (to_ - from_) * std::min(1.0, double(polls_) / double(items_)));
        } catch (const TerrainCancelled &) {
            aborted_ = true;
        }
        return aborted_;
    }
    // The end of a phase: throws when the phase was aborted, otherwise reports its end.
    void finish() const {
        if (aborted_)
            throw TerrainCancelled();
        steps_(to_);
    }

  private:
    Steps steps_;
    double from_{}, to_{};
    std::size_t items_{1};
    mutable std::size_t polls_{};
    mutable bool aborted_{};
};

// Constrained Delaunay of `points` with `edges` (indices into `points`). The super triangle
// is erased; the convex hull remains. With a TerrainProgress, the run reports `from` to `to`
// and is cancellable within vertex and edge insertion (TerrainCancelled).
void runCdt(const std::vector<Pt> &points, const std::vector<std::pair<u32, u32>> &edges,
            CdtTriangulation &tri, Steps steps = {nullptr}, double from = 0, double to = 0) {
    std::vector<CDT::V2d<double>> vertices;
    vertices.reserve(points.size());
    for (const auto &p : points)
        vertices.push_back(CDT::V2d<double>(p.x, p.y));
    std::vector<CDT::Edge> constraints;
    constraints.reserve(edges.size());
    for (const auto &e : edges)
        constraints.emplace_back(CDT::VertInd(e.first), CDT::VertInd(e.second));
    if (!steps.progress) {
        tri.insertVertices(vertices);
        tri.insertEdges(constraints);
        tri.eraseSuperTriangle();
        return;
    }
    // Measured shares at 250,000 triangles: vertices about 70 %, edges 10 %, erase 20 %.
    const double span = to - from;
    CdtCheck check(steps);
    struct Detach {
        CdtTriangulation &tri;
        ~Detach() { tri.setCallbackHandler(nullptr); }
    } detach{tri};
    tri.setCallbackHandler(&check);
    check.phase(from, from + span * .7, vertices.size());
    tri.insertVertices(vertices);
    check.finish();
    check.phase(from + span * .7, from + span * .8, constraints.size());
    tri.insertEdges(constraints);
    check.finish();
    tri.eraseSuperTriangle();
    steps(to);
}

Mesh meshFrom(const CdtTriangulation &tri) {
    Mesh m;
    m.v.reserve(tri.triangles.size());
    for (const auto &t : tri.triangles)
        m.v.push_back({u32(t.vertices[0]), u32(t.vertices[1]), u32(t.vertices[2])});
    return m;
}

// Elevation of `p` in the triangulation of the sources: barycentric inside the hull, the
// nearest source vertex outside it. `extrapolated` reports which.
class Elevation {
  public:
    Elevation(const std::vector<Pt> &xy, const std::vector<double> &z, Mesh mesh)
        : xy_(xy), z_(z), mesh_(std::move(mesh)) {
        vertexTri_.assign(xy_.size(), kNone);
        for (u32 t = 0; t < mesh_.v.size(); ++t)
            for (u32 v : mesh_.v[t])
                if (vertexTri_[v] == kNone)
                    vertexTri_[v] = t;
        points_.reserve(xy_.size());
        for (const auto &p : xy_)
            points_.push_back(CDT::V2d<double>(p.x, p.y));
        tree_.initialize(points_);
    }
    double at(const Pt &p, bool &extrapolated) {
        const u32 nearest = tree_.nearPoint(CDT::V2d<double>(p.x, p.y), points_);
        u32 t = vertexTri_[nearest];
        bool inside = false;
        if (t != kNone) {
            const std::size_t limit = mesh_.v.size() * 3 + 16;
            for (std::size_t step = 0; step < limit; ++step) {
                u32 next = kNone;
                bool outer = false;
                for (u32 i = 0; i < 3; ++i) {
                    if (orientSign(xy_[mesh_.v[t][i]], xy_[mesh_.v[t][(i + 1) % 3]], p) < 0) {
                        if (mesh_.nb[t][i] == kNone)
                            outer = true;
                        else
                            next = mesh_.nb[t][i];
                        break;
                    }
                }
                if (outer) {
                    t = kNone;
                    break;
                }
                if (next == kNone) {
                    inside = true;
                    break;
                }
                t = next;
            }
            if (!inside && t != kNone) { // the walk did not settle: scan, which is always right
                t = kNone;
                for (u32 k = 0; k < mesh_.v.size() && !inside; ++k)
                    if (orientSign(xy_[mesh_.v[k][0]], xy_[mesh_.v[k][1]], p) >= 0 &&
                        orientSign(xy_[mesh_.v[k][1]], xy_[mesh_.v[k][2]], p) >= 0 &&
                        orientSign(xy_[mesh_.v[k][2]], xy_[mesh_.v[k][0]], p) >= 0) {
                        t = k;
                        inside = true;
                    }
            }
        }
        if (!inside) {
            extrapolated = true;
            return z_[nearest];
        }
        extrapolated = false;
        const auto &v = mesh_.v[t];
        const double w0 = orientValue(xy_[v[1]], xy_[v[2]], p),
                     w1 = orientValue(xy_[v[2]], xy_[v[0]], p),
                     w2 = orientValue(xy_[v[0]], xy_[v[1]], p);
        const double sum = w0 + w1 + w2;
        return sum > 0 ? (w0 * z_[v[0]] + w1 * z_[v[1]] + w2 * z_[v[2]]) / sum : z_[v[0]];
    }

  private:
    const std::vector<Pt> &xy_;
    const std::vector<double> &z_;
    Mesh mesh_;
    std::vector<u32> vertexTri_;
    std::vector<CDT::V2d<double>> points_;
    CDT::LocatorKDTree<double> tree_;
};

} // namespace

namespace {
TerrainTriangulationResult triangulate(const TerrainTriangulationInput &input,
                                       const TerrainTriangulationOptions &options,
                                       const Steps step) {
    TerrainTriangulationResult result;
    auto &report = result.report;
    Problems problems;
    const auto &limits = options.limits;
    const auto refuse = [&]() -> TerrainTriangulationResult {
        report.refusals = problems.finish();
        result.vertices.clear();
        result.triangles.clear();
        result.ok = false;
        return std::move(result);
    };

    // ---- 1. Numbers and sizes ------------------------------------------------------------
    step(0);
    std::size_t raw = input.points.size();
    for (const auto &c : input.contours)
        raw += c.points.size();
    for (const auto &b : input.breaklines)
        raw += b.points.size();
    report.rawVertexCount = raw;
    std::size_t polygonVertices = input.boundary ? input.boundary->points.size() : 0;
    for (const auto &h : input.holes)
        polygonVertices += h.points.size();
    if (raw > limits.maxRawVertices) {
        problems.add(TerrainProblemKind::LimitExceeded);
        problems.detail(TerrainProblemKind::LimitExceeded,
                        format("%llu input vertices before merging; the limit is %llu.", raw,
                               limits.maxRawVertices));
        return refuse();
    }
    if (polygonVertices > limits.maxPolygonVertices) {
        problems.add(TerrainProblemKind::LimitExceeded);
        problems.detail(TerrainProblemKind::LimitExceeded,
                        format("%llu boundary and hole vertices; the limit is %llu.",
                               polygonVertices, limits.maxPolygonVertices));
        return refuse();
    }
    {
        const auto check = [&](double x, double y, const double *z, u64 source) {
            const double values[3] = {x, y, z ? *z : 0.0};
            for (double v : values)
                if (!std::isfinite(v)) {
                    problems.add(TerrainProblemKind::NonFinite,
                                 Pt{std::isfinite(x) ? x : 0, std::isfinite(y) ? y : 0}, source);
                    return;
                }
            if (std::abs(x) > limits.maxCoordinate || std::abs(y) > limits.maxCoordinate ||
                (z && std::abs(*z) > limits.maxCoordinate)) {
                problems.add(TerrainProblemKind::ExtentExceeded, Pt{x, y}, source);
            }
        };
        for (const auto &c : input.contours)
            for (const auto &p : c.points)
                check(p.x, p.y, &c.z, c.sourceId);
        for (const auto &b : input.breaklines)
            for (const auto &p : b.points)
                check(p.x, p.y, &p.z, b.sourceId);
        for (const auto &p : input.points)
            check(p.position.x, p.position.y, &p.position.z, p.sourceId);
        if (input.boundary)
            for (const auto &p : input.boundary->points)
                check(p.x, p.y, nullptr, input.boundary->sourceId);
        for (const auto &h : input.holes)
            for (const auto &p : h.points)
                check(p.x, p.y, nullptr, h.sourceId);
        if (problems.has(TerrainProblemKind::ExtentExceeded))
            problems.detail(TerrainProblemKind::ExtentExceeded,
                            format("%llu coordinate(s) are beyond +/-%llu m; convert to the "
                                   "terrain's local frame first.",
                                   problems.count(TerrainProblemKind::ExtentExceeded),
                                   (unsigned long long)limits.maxCoordinate));
        if (problems.any())
            return refuse();
    }
    step(.02);

    // ---- 2. Vertices and polylines ---------------------------------------------------------
    VertexStore store(options.xyTolerance, options.zTolerance);
    std::vector<Segment> segments;
    std::size_t visited = 0;
    const auto addVertex = [&](double x, double y, double z, bool hasZ, bool contour, u64 source) {
        step.every(visited++, raw, .02, .10);
        const auto added = store.add(x, y, z, hasZ, contour, source);
        if (added.merged)
            ++report.mergedDuplicates;
        if (added.conflict)
            problems.add(TerrainProblemKind::ConflictingDuplicate, Pt{x, y},
                         store.source(added.other), source);
        return added.id;
    };
    const auto addPolyline = [&](const std::vector<u32> &ids, bool closed, u64 source) {
        const std::size_t count = ids.size();
        const std::size_t last = closed && count > 2 ? count : count - (count ? 1 : 0);
        for (std::size_t i = 0; i < last; ++i) {
            const u32 a = ids[i], b = ids[(i + 1) % count];
            if (a == b)
                ++report.droppedZeroLengthSegments;
            else
                segments.push_back({a, b, DataSegment, source});
        }
    };
    for (const auto &c : input.contours) {
        std::vector<u32> ids;
        for (const auto &p : c.points)
            ids.push_back(addVertex(p.x, p.y, c.z, true, true, c.sourceId));
        addPolyline(ids, c.closed, c.sourceId);
    }
    for (const auto &b : input.breaklines) {
        std::vector<u32> ids;
        for (const auto &p : b.points)
            ids.push_back(addVertex(p.x, p.y, p.z, true, false, b.sourceId));
        addPolyline(ids, b.closed, b.sourceId);
    }
    for (const auto &p : input.points)
        addVertex(p.position.x, p.position.y, p.position.z, true, false, p.sourceId);
    const std::size_t sourceVertices = store.xy.size();
    if (sourceVertices > limits.maxVertices) {
        problems.add(TerrainProblemKind::LimitExceeded);
        problems.detail(TerrainProblemKind::LimitExceeded,
                        format("%llu vertices after merging; the limit is %llu.", sourceVertices,
                               limits.maxVertices));
        return refuse();
    }

    // ---- 3. Boundary, holes, and what they drop --------------------------------------------
    struct Polygon {
        const TerrainPolygon *input;
        std::vector<Pt> raw;
        std::vector<u32> ids;
        bool dropped{false};
        bool hole{false};
    };
    std::vector<Polygon> polygons;
    const auto prepare = [&](const TerrainPolygon &p, bool hole) {
        Polygon poly;
        poly.input = &p;
        poly.hole = hole;
        for (const auto &q : p.points)
            poly.raw.push_back({q.x, q.y});
        // At least three distinct, not all collinear, vertices.
        std::size_t distinct = 0;
        bool area = false;
        for (std::size_t i = 0; i < poly.raw.size(); ++i) {
            if (i == 0 || (std::abs(poly.raw[i].x - poly.raw[i - 1].x) > options.xyTolerance ||
                           std::abs(poly.raw[i].y - poly.raw[i - 1].y) > options.xyTolerance))
                ++distinct;
            if (i >= 2 && orientSign(poly.raw[0], poly.raw[1], poly.raw[i]) != 0)
                area = true;
        }
        if (distinct < 3 || !area) {
            problems.add(TerrainProblemKind::InvalidPolygon,
                         poly.raw.empty() ? Pt{} : poly.raw.front(), p.sourceId);
            return;
        }
        polygons.push_back(std::move(poly));
    };
    if (input.boundary)
        prepare(*input.boundary, false);
    for (const auto &h : input.holes)
        prepare(h, true);
    const bool hasBoundary = input.boundary && !polygons.empty() && !polygons.front().hole;
    {
        std::vector<PolygonIndex> index;
        index.reserve(polygons.size());
        for (const auto &p : polygons)
            index.emplace_back(p.raw);
        // A hole entirely outside the boundary is dropped; one around it is an error; a
        // hole inside another hole is not supported (ADR 0168 section 3).
        for (std::size_t i = hasBoundary ? 1 : 0; i < polygons.size(); ++i) {
            auto &hole = polygons[i];
            if (hasBoundary) {
                std::size_t outside = 0;
                for (const auto &q : hole.raw)
                    outside += index[0].locate(q) == PolygonIndex::Where::Outside;
                if (outside == hole.raw.size()) {
                    if (index[i].locate(polygons[0].raw.front()) == PolygonIndex::Where::Inside)
                        problems.add(TerrainProblemKind::InvalidPolygon, hole.raw.front(),
                                     hole.input->sourceId, polygons[0].input->sourceId);
                    hole.dropped = true;
                    ++report.droppedHoles;
                    continue;
                }
            }
            for (std::size_t j = hasBoundary ? 1 : 0; j < polygons.size(); ++j)
                if (j != i && !polygons[j].dropped &&
                    index[j].locate(hole.raw.front()) == PolygonIndex::Where::Inside)
                    problems.add(TerrainProblemKind::InvalidPolygon, hole.raw.front(),
                                 hole.input->sourceId, polygons[j].input->sourceId);
        }
        if (problems.has(TerrainProblemKind::InvalidPolygon)) {
            // Fall through so other problems are still found, without the broken polygons.
            polygons.clear();
        } else {
            // Polygon vertices join the store after the sources, so source numbering is unchanged.
            for (auto &poly : polygons) {
                if (poly.dropped)
                    continue;
                for (const auto &q : poly.raw) {
                    const auto added = store.add(q.x, q.y, 0, false, false, poly.input->sourceId);
                    if (added.merged)
                        ++report.mergedDuplicates;
                    store.flags[added.id] |= OnPolygon;
                    poly.ids.push_back(added.id);
                }
            }
            // Sources outside the boundary or inside a hole are dropped.
            for (u32 v = 0; v < sourceVertices; ++v) {
                step.every(v, sourceVertices, .10, .13);
                if (store.flags[v] & OnPolygon)
                    continue;
                bool drop = false;
                for (std::size_t i = 0; i < polygons.size() && !drop; ++i) {
                    if (polygons[i].dropped)
                        continue;
                    const auto where = index[i].locate(store.xy[v]);
                    drop = polygons[i].hole ? where == PolygonIndex::Where::Inside
                                            : where == PolygonIndex::Where::Outside;
                }
                if (drop) {
                    store.flags[v] |= Dropped;
                    ++report.droppedOutsideVertices;
                }
            }
        }
    }
    step(.13);
    const std::size_t total = store.xy.size();
    auto &flags = store.flags;
    // Segments lose their dropped endpoints; polygon edges join as region segments.
    {
        std::vector<Segment> kept;
        for (const auto &s : segments) {
            if ((flags[s.a] & Dropped) || (flags[s.b] & Dropped))
                ++report.droppedOutsideSegments;
            else
                kept.push_back(s);
        }
        segments = std::move(kept);
        for (const auto &poly : polygons) {
            if (poly.dropped)
                continue;
            const std::size_t n = poly.ids.size();
            for (std::size_t i = 0; i < n; ++i) {
                const u32 a = poly.ids[i], b = poly.ids[(i + 1) % n];
                if (a == b)
                    ++report.droppedZeroLengthSegments;
                else
                    segments.push_back({a, b, RegionSegment, poly.input->sourceId});
            }
        }
        dedupe(segments, store.xy, problems, report);
    }
    step(.15);

    // ---- 4. Crossing, touching and overlapping constraints -----------------------------------
    // Exact predicates throughout. A vertex strictly inside a segment is a touch: allowed only
    // on a region segment, which is split there. Two segments with no common endpoint that
    // properly cross are an error. Segments sharing an endpoint cannot properly cross; if they
    // overlap, an endpoint of one lies inside the other, which is a touch.
    std::vector<std::vector<u32>> splits(segments.size());
    if (!segments.empty()) {
        Grid grid(store.xy, flags, std::max(segments.size(), total));
        const auto cellCount = std::size_t(grid.cells());
        CellLists vertexCells;
        vertexCells.start.assign(cellCount + 1, 0);
        for (u32 v = 0; v < total; ++v)
            if (!(flags[v] & Dropped))
                ++vertexCells.start[std::size_t(grid.cell(store.xy[v])) + 1];
        for (std::size_t c = 0; c < cellCount; ++c)
            vertexCells.start[c + 1] += vertexCells.start[c];
        vertexCells.items.resize(vertexCells.start[cellCount]);
        {
            std::vector<u32> fill(cellCount, 0);
            for (u32 v = 0; v < total; ++v)
                if (!(flags[v] & Dropped)) {
                    const auto c = std::size_t(grid.cell(store.xy[v]));
                    vertexCells.items[vertexCells.start[c] + fill[c]++] = v;
                }
        }
        step(.16);
        CellLists segmentCells;
        segmentCells.start.assign(cellCount + 1, 0);
        std::size_t entries = 0;
        for (const auto &s : segments)
            grid.raster(store.xy[s.a], store.xy[s.b], [&](int c) {
                ++segmentCells.start[std::size_t(c) + 1];
                ++entries;
            });
        if (entries > kMaxGridEntries) {
            problems.add(TerrainProblemKind::LimitExceeded);
            problems.detail(
                TerrainProblemKind::LimitExceeded,
                format(
                    "The constraint segments are too long and dense to check (%llu cell entries).",
                    entries));
            return refuse();
        }
        for (std::size_t c = 0; c < cellCount; ++c)
            segmentCells.start[c + 1] += segmentCells.start[c];
        segmentCells.items.resize(entries);
        {
            std::vector<u32> fill(cellCount, 0);
            for (u32 i = 0; i < segments.size(); ++i)
                grid.raster(store.xy[segments[i].a], store.xy[segments[i].b], [&](int c) {
                    segmentCells
                        .items[segmentCells.start[std::size_t(c)] + fill[std::size_t(c)]++] = i;
                });
        }
        step(.17);
        // Vertices inside segments.
        std::vector<std::pair<u32, u32>> touches; // (segment, vertex)
        for (u32 i = 0; i < segments.size(); ++i) {
            step.every(i, segments.size(), .17, .20);
            const Pt &a = store.xy[segments[i].a];
            const Pt &b = store.xy[segments[i].b];
            grid.raster(a, b, [&](int c) {
                for (u32 k = vertexCells.start[std::size_t(c)];
                     k < vertexCells.start[std::size_t(c) + 1]; ++k) {
                    const u32 v = vertexCells.items[k];
                    if (v != segments[i].a && v != segments[i].b && inBox(a, b, store.xy[v]) &&
                        orientSign(a, b, store.xy[v]) == 0)
                        touches.emplace_back(i, v);
                }
            });
        }
        std::sort(touches.begin(), touches.end());
        touches.erase(std::unique(touches.begin(), touches.end()), touches.end());
        for (const auto &[i, v] : touches) {
            if (segments[i].kind & RegionSegment)
                splits[i].push_back(v);
            else
                problems.add(TerrainProblemKind::CrossingConstraints, store.xy[v],
                             segments[i].source, store.source(v));
        }
        // Segment pairs that properly cross.
        std::vector<u64> pairs;
        for (std::size_t c = 0; c < cellCount && pairs.size() <= kMaxCrossingPairs; ++c) {
            step.every(c, cellCount, .20, .24);
            for (u32 x = segmentCells.start[c]; x < segmentCells.start[c + 1]; ++x) {
                const auto &s = segments[segmentCells.items[x]];
                const Pt &a = store.xy[s.a], &b = store.xy[s.b];
                for (u32 y = x + 1; y < segmentCells.start[c + 1]; ++y) {
                    const auto &r = segments[segmentCells.items[y]];
                    if (r.a == s.a || r.a == s.b || r.b == s.a || r.b == s.b)
                        continue;
                    const Pt &c0 = store.xy[r.a], &d0 = store.xy[r.b];
                    if (std::max(a.x, b.x) < std::min(c0.x, d0.x) ||
                        std::max(c0.x, d0.x) < std::min(a.x, b.x) ||
                        std::max(a.y, b.y) < std::min(c0.y, d0.y) ||
                        std::max(c0.y, d0.y) < std::min(a.y, b.y))
                        continue;
                    const int o1 = orientSign(a, b, c0), o2 = orientSign(a, b, d0);
                    if (o1 * o2 >= 0)
                        continue;
                    if (orientSign(c0, d0, a) * orientSign(c0, d0, b) >= 0)
                        continue;
                    pairs.push_back(u64(segmentCells.items[x]) << 32 | segmentCells.items[y]);
                }
            }
        }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        for (u64 pair : pairs) {
            const auto &s = segments[pair >> 32];
            const auto &r = segments[pair & 0xffffffffu];
            const Pt &a = store.xy[s.a], &b = store.xy[s.b], &c0 = store.xy[r.a],
                     &d0 = store.xy[r.b];
            const double u = orientValue(c0, d0, a), w = orientValue(c0, d0, b);
            const double t = (u - w) != 0 ? u / (u - w) : 0.5;
            problems.add(TerrainProblemKind::CrossingConstraints,
                         Pt{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)}, s.source, r.source);
        }
        if (pairs.size() > kMaxCrossingPairs)
            problems.detail(TerrainProblemKind::CrossingConstraints,
                            "More than 1000000 constraint segment pairs cross, touch or overlap; "
                            "counting stopped.");
        // Split region segments at the vertices that touch them.
        std::vector<Segment> pieces;
        pieces.reserve(segments.size());
        for (u32 i = 0; i < segments.size(); ++i) {
            if (splits[i].empty()) {
                pieces.push_back(segments[i]);
                continue;
            }
            const Pt a = store.xy[segments[i].a];
            auto &list = splits[i];
            std::sort(list.begin(), list.end(), [&](u32 l, u32 r) {
                const double dl = std::hypot(store.xy[l].x - a.x, store.xy[l].y - a.y);
                const double dr = std::hypot(store.xy[r].x - a.x, store.xy[r].y - a.y);
                return dl != dr ? dl < dr : l < r;
            });
            u32 previous = segments[i].a;
            for (u32 v : list) {
                pieces.push_back({previous, v, segments[i].kind, segments[i].source});
                previous = v;
            }
            pieces.push_back({previous, segments[i].b, segments[i].kind, segments[i].source});
        }
        segments = std::move(pieces);
        dedupe(segments, store.xy, problems, report);
    }

    step(.25);
    // ---- 5. Enough points ---------------------------------------------------------------------
    std::vector<u32> sourceIds; // kept source vertices, with an elevation
    for (u32 v = 0; v < sourceVertices; ++v)
        if (!(flags[v] & Dropped))
            sourceIds.push_back(v);
    {
        bool spread = false;
        for (std::size_t i = 2; i < sourceIds.size() && !spread; ++i)
            spread = orientSign(store.xy[sourceIds[0]], store.xy[sourceIds[1]],
                                store.xy[sourceIds[i]]) != 0;
        if (!spread) {
            for (std::size_t i = 0; i < sourceIds.size() && i < terrainMaxLocationsPerKind; ++i)
                problems.add(TerrainProblemKind::InsufficientPoints, store.xy[sourceIds[i]],
                             store.source(sourceIds[i]));
            problems.setCount(TerrainProblemKind::InsufficientPoints,
                              std::max<std::size_t>(sourceIds.size(), 1));
            problems.detail(
                TerrainProblemKind::InsufficientPoints,
                format("Fewer than three non-collinear points (%llu usable).", sourceIds.size()));
        }
    }
    report.uniqueVertexCount = total - report.droppedOutsideVertices;
    if (problems.any())
        return refuse();

    // ---- 6. Elevations for polygon vertices
    // ------------------------------------------------------
    {
        bool pending = false;
        for (u32 v = 0; v < total; ++v)
            pending = pending || (!(flags[v] & Dropped) && !(flags[v] & HasZ));
        if (pending) {
            std::vector<u32> toFirst(total, kNone);
            std::vector<Pt> pts;
            std::vector<double> zs;
            // All sources take part, including those about to be dropped: they say what the
            // terrain does at the boundary. Only the kept segments constrain it.
            for (u32 v = 0; v < sourceVertices; ++v) {
                toFirst[v] = u32(pts.size());
                pts.push_back(store.xy[v]);
                zs.push_back(store.z[v]);
            }
            std::vector<std::pair<u32, u32>> edges;
            for (const auto &s : segments)
                if ((s.kind & DataSegment) && toFirst[s.a] != kNone && toFirst[s.b] != kNone)
                    edges.emplace_back(toFirst[s.a], toFirst[s.b]);
            CdtTriangulation first(CDT::VertexInsertionOrder::Auto,
                                   CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
            try {
                runCdt(pts, edges, first, step, .26, .32);
            } catch (const CDT::Error &) {
                problems.add(TerrainProblemKind::TriangulationFailed);
                return refuse();
            }
            Mesh mesh = meshFrom(first);
            if (!buildNeighbors(mesh)) {
                problems.add(TerrainProblemKind::TriangulationFailed);
                return refuse();
            }
            Elevation elevation(pts, zs, std::move(mesh));
            for (u32 v = 0; v < total; ++v)
                if (!(flags[v] & Dropped) && !(flags[v] & HasZ)) {
                    step.every(v, total, .32, .34);
                    bool extrapolated = false;
                    store.z[v] = elevation.at(store.xy[v], extrapolated);
                    flags[v] |= HasZ;
                    ++report.interpolatedPolygonVertices;
                    report.extrapolatedPolygonVertices += extrapolated;
                }
        }
    }

    step(.34);
    // ---- 7. Constrained Delaunay
    // -------------------------------------------------------------------
    std::vector<u32> packed(total, kNone);
    Surface surface;
    for (u32 v = 0; v < total; ++v) {
        if (flags[v] & Dropped)
            continue;
        packed[v] = u32(surface.xy.size());
        surface.xy.push_back(store.xy[v]);
        surface.z.push_back(store.z[v]);
        surface.contour.push_back((flags[v] & OnContour) ? 1 : 0);
    }
    std::vector<std::pair<u32, u32>> edges;
    std::vector<u64> fixedKeys, regionKeys;
    for (const auto &s : segments) {
        edges.emplace_back(packed[s.a], packed[s.b]);
        fixedKeys.push_back(edgeKey(packed[s.a], packed[s.b]));
        if (s.kind & RegionSegment)
            regionKeys.push_back(edgeKey(packed[s.a], packed[s.b]));
    }
    std::sort(fixedKeys.begin(), fixedKeys.end());
    std::sort(regionKeys.begin(), regionKeys.end());
    report.constraintEdges = edges.size();
    Mesh mesh;
    {
        CdtTriangulation tri(CDT::VertexInsertionOrder::Auto,
                             CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
        try {
            runCdt(surface.xy, edges, tri, step, .35, .80);
        } catch (const CDT::IntersectingConstraintsError &) {
            problems.add(TerrainProblemKind::CrossingConstraints);
            problems.detail(TerrainProblemKind::CrossingConstraints,
                            "The triangulation library found crossing constraint segments.");
            return refuse();
        } catch (const CDT::Error &) {
            problems.add(TerrainProblemKind::TriangulationFailed);
            return refuse();
        }
        // The library must keep every constraint as one edge; a split would break the region rule.
        bool intact = tri.fixedEdges.size() == edges.size();
        for (const auto &e : edges)
            intact = intact && tri.fixedEdges.count(
                                   CDT::Edge(CDT::VertInd(e.first), CDT::VertInd(e.second))) != 0;
        if (!intact || tri.vertices.size() != surface.xy.size()) {
            problems.add(TerrainProblemKind::TriangulationFailed);
            return refuse();
        }
        mesh = meshFrom(tri);
    }
    step(.81);
    if (!buildNeighbors(mesh)) {
        problems.add(TerrainProblemKind::TriangulationFailed);
        return refuse();
    }
    step(.83);

    // ---- 8. Outside the boundary and inside holes
    // --------------------------------------------------- Depth by region edges, from the outside
    // of the hull: crossing a region edge adds one, and without a boundary the hull itself counts
    // as one. Odd depth is terrain.
    {
        const std::size_t count = mesh.v.size();
        std::vector<int> depth(count, -1);
        using Item = std::pair<int, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
        const auto isRegion = [&](const Tri &t, u32 i) {
            return sortedContains(regionKeys, edgeKey(t[i], t[(i + 1) % 3]));
        };
        for (u32 t = 0; t < count; ++t)
            for (u32 i = 0; i < 3; ++i)
                if (mesh.nb[t][i] == kNone) {
                    const int d = (!hasBoundary || isRegion(mesh.v[t], i)) ? 1 : 0;
                    if (depth[t] < 0 || d < depth[t]) {
                        depth[t] = d;
                        queue.emplace(d, t);
                    }
                }
        std::size_t popped = 0;
        while (!queue.empty()) {
            step.every(popped++, 2 * count, .83, .85);
            const auto [d, t] = queue.top();
            queue.pop();
            if (d != depth[t])
                continue;
            for (u32 i = 0; i < 3; ++i) {
                const u32 n = mesh.nb[t][i];
                if (n == kNone)
                    continue;
                const int nd = d + (isRegion(mesh.v[t], i) ? 1 : 0);
                if (depth[n] < 0 || nd < depth[n]) {
                    depth[n] = nd;
                    queue.emplace(nd, n);
                }
            }
        }
        Mesh kept;
        for (u32 t = 0; t < count; ++t)
            if (depth[t] >= 0 && (depth[t] & 1))
                kept.v.push_back(mesh.v[t]);
        mesh = std::move(kept);
        if (!buildNeighbors(mesh)) {
            problems.add(TerrainProblemKind::TriangulationFailed);
            return refuse();
        }
    }

    step(.86);
    // ---- 9. Trim long outer edges
    // ---------------------------------------------------------------------- Never across a
    // constraint: contour, breakline, boundary and hole edges are data.
    if (options.maxEdgeLength && *options.maxEdgeLength > 0) {
        const double limit = *options.maxEdgeLength;
        std::vector<char> removed(mesh.v.size(), 0);
        std::vector<u32> pending;
        const auto longOuter = [&](u32 t) {
            for (u32 i = 0; i < 3; ++i) {
                const u32 n = mesh.nb[t][i];
                if (n != kNone && !removed[n])
                    continue;
                const u32 a = mesh.v[t][i], b = mesh.v[t][(i + 1) % 3];
                if (!sortedContains(fixedKeys, edgeKey(a, b)) &&
                    std::hypot(surface.xy[a].x - surface.xy[b].x,
                               surface.xy[a].y - surface.xy[b].y) > limit)
                    return true;
            }
            return false;
        };
        for (u32 t = 0; t < mesh.v.size(); ++t)
            if (longOuter(t))
                pending.push_back(t);
        for (std::size_t k = 0; k < pending.size(); ++k) {
            const u32 t = pending[k];
            if (removed[t] || !longOuter(t))
                continue;
            removed[t] = 1;
            ++report.trimmedTriangles;
            for (u32 i = 0; i < 3; ++i) {
                const u32 n = mesh.nb[t][i];
                if (n != kNone && !removed[n])
                    pending.push_back(n);
            }
        }
        if (report.trimmedTriangles) {
            Mesh kept;
            for (u32 t = 0; t < mesh.v.size(); ++t)
                if (!removed[t])
                    kept.v.push_back(mesh.v[t]);
            mesh = std::move(kept);
            if (!buildNeighbors(mesh)) {
                problems.add(TerrainProblemKind::TriangulationFailed);
                return refuse();
            }
        }
    }
    const auto budget = [&]() {
        if (mesh.v.size() <= limits.maxTriangles)
            return true;
        problems.add(TerrainProblemKind::TriangleBudget);
        problems.detail(TerrainProblemKind::TriangleBudget,
                        format("The terrain has %llu triangles; the limit is %llu.", mesh.v.size(),
                               limits.maxTriangles));
        return false;
    };
    step(.88);
    if (!budget())
        return refuse();
    if (mesh.v.empty()) {
        problems.add(TerrainProblemKind::InsufficientPoints);
        problems.detail(TerrainProblemKind::InsufficientPoints,
                        "No triangles remain after the boundary, holes and edge limit.");
        return refuse();
    }

    // ---- 10. Flat triangles
    // --------------------------------------------------------------------------------
    const double flatTol = options.flatTolerance;
    for (const auto &t : mesh.v)
        report.flatTrianglesInitial += isFlat(surface, t, flatTol);
    if (options.flatMode == TerrainFlatMode::Flip && report.flatTrianglesInitial) {
        // A flip is applied when it keeps every constraint (constraint edges are never flipped),
        // the quadrilateral is strictly convex, and it leaves fewer flat triangles in the pair.
        // Each flip lowers the flat count, so the loop ends; the queue order is fixed.
        std::vector<u32> queue;
        std::vector<char> queued(mesh.v.size(), 0);
        for (u32 t = 0; t < mesh.v.size(); ++t)
            if (isFlat(surface, mesh.v[t], flatTol)) {
                queue.push_back(t);
                queued[t] = 1;
            }
        for (std::size_t head = 0; head < queue.size(); ++head) {
            step.every(head, queue.size(), .88, .94);
            const u32 t = queue[head];
            queued[t] = 0;
            if (!isFlat(surface, mesh.v[t], flatTol))
                continue;
            for (u32 slot = 0; slot < 3; ++slot) {
                const u32 n = mesh.nb[t][slot];
                if (n == kNone ||
                    sortedContains(fixedKeys, edgeKey(mesh.v[t][slot], mesh.v[t][(slot + 1) % 3])))
                    continue;
                bool flipped = false;
                if (!flipEdge(mesh, surface, t, slot, flatTol, true, flipped)) {
                    problems.add(TerrainProblemKind::TriangulationFailed);
                    return refuse();
                }
                if (!flipped)
                    continue;
                ++report.flipsApplied;
                std::vector<u32> touched{t, n};
                for (u32 w : {t, n})
                    for (u32 i = 0; i < 3; ++i)
                        if (mesh.nb[w][i] != kNone)
                            touched.push_back(mesh.nb[w][i]);
                for (u32 w : touched)
                    if (!queued[w] && isFlat(surface, mesh.v[w], flatTol)) {
                        queued[w] = 1;
                        queue.push_back(w);
                    }
                break;
            }
        }
    }
    if (options.steinerFill) {
        // Split each remaining flat triangle at the midpoint of its longest edge that is not a
        // constraint and whose neighbour's opposite vertex has a different elevation; the new
        // point's elevation is halfway to that vertex. The neighbour is never flat then, so each
        // split touches exactly one flat triangle and the list built first stays valid.
        std::vector<u32> flat;
        for (u32 t = 0; t < mesh.v.size(); ++t)
            if (isFlat(surface, mesh.v[t], flatTol))
                flat.push_back(t);
        for (std::size_t k = 0; k < flat.size(); ++k) {
            step.every(k, flat.size(), .94, .97);
            const u32 t = flat[k];
            if (!isFlat(surface, mesh.v[t], flatTol))
                continue;
            u32 best = kNone;
            double bestLength = -1;
            for (u32 slot = 0; slot < 3; ++slot) {
                const u32 n = mesh.nb[t][slot];
                const u32 a = mesh.v[t][slot], b = mesh.v[t][(slot + 1) % 3];
                if (n == kNone || sortedContains(fixedKeys, edgeKey(a, b)))
                    continue;
                u32 d = kNone;
                for (u32 w : mesh.v[n])
                    if (w != a && w != b)
                        d = w;
                if (d == kNone || std::abs(surface.z[d] - surface.z[a]) <= flatTol)
                    continue;
                const double length = std::hypot(surface.xy[a].x - surface.xy[b].x,
                                                 surface.xy[a].y - surface.xy[b].y);
                if (length > bestLength) {
                    bestLength = length;
                    best = slot;
                }
            }
            if (best == kNone)
                continue;
            const u32 a = mesh.v[t][best], b = mesh.v[t][(best + 1) % 3],
                      c = mesh.v[t][(best + 2) % 3];
            const u32 n = mesh.nb[t][best];
            u32 d = kNone;
            for (u32 w : mesh.v[n])
                if (w != a && w != b)
                    d = w;
            const Pt mid{(surface.xy[a].x + surface.xy[b].x) / 2,
                         (surface.xy[a].y + surface.xy[b].y) / 2};
            // All four pieces must stay counter-clockwise (rounding of the midpoint).
            if (orientSign(surface.xy[a], mid, surface.xy[c]) <= 0 ||
                orientSign(mid, surface.xy[b], surface.xy[c]) <= 0 ||
                orientSign(surface.xy[b], mid, surface.xy[d]) <= 0 ||
                orientSign(mid, surface.xy[a], surface.xy[d]) <= 0)
                continue;
            const u32 m = u32(surface.xy.size());
            surface.xy.push_back(mid);
            surface.z.push_back((surface.z[a] + surface.z[d]) / 2);
            surface.contour.push_back(0);
            ++report.steinerPointsAdded;
            const auto across = [&](u32 tri, u32 p, u32 q) {
                for (u32 i = 0; i < 3; ++i)
                    if (mesh.v[tri][i] == p && mesh.v[tri][(i + 1) % 3] == q)
                        return mesh.nb[tri][i];
                return kNone;
            };
            const u32 nbBC = across(t, b, c), nbCA = across(t, c, a), nbAD = across(n, a, d),
                      nbDB = across(n, d, b);
            const u32 t2 = u32(mesh.v.size()), n1 = t2 + 1;
            mesh.v.resize(mesh.v.size() + 2);
            mesh.nb.resize(mesh.nb.size() + 2);
            mesh.v[t] = {a, m, c};
            mesh.nb[t] = {n1, t2, nbCA};
            mesh.v[t2] = {m, b, c};
            mesh.nb[t2] = {n, nbBC, t};
            mesh.v[n] = {b, m, d};
            mesh.nb[n] = {t2, n1, nbDB};
            mesh.v[n1] = {m, a, d};
            mesh.nb[n1] = {t, nbAD, n};
            relink(mesh, nbBC, b, c, t2);
            relink(mesh, nbAD, a, d, n1);
        }
        if (!budget())
            return refuse();
    }
    for (const auto &t : mesh.v)
        if (isFlat(surface, t, flatTol)) {
            ++report.flatTrianglesRemaining;
            report.flatAreaRemaining += planArea(surface, t);
            if (report.flatLocations.size() < terrainMaxLocationsPerKind)
                report.flatLocations.push_back(
                    {(surface.xy[t[0]].x + surface.xy[t[1]].x + surface.xy[t[2]].x) / 3,
                     (surface.xy[t[0]].y + surface.xy[t[1]].y + surface.xy[t[2]].y) / 3});
        }

    step(.97);
    // ---- 11. Output
    // ---------------------------------------------------------------------------------------------
    std::vector<u32> remap(surface.xy.size(), kNone);
    for (const auto &t : mesh.v)
        for (u32 v : t)
            remap[v] = 0;
    for (u32 v = 0, next = 0; v < remap.size(); ++v) {
        if (remap[v] == kNone) {
            ++report.unusedVertices;
            continue;
        }
        remap[v] = next++;
        result.vertices.push_back({surface.xy[v].x, surface.xy[v].y, surface.z[v]});
    }
    result.triangles.reserve(mesh.v.size());
    for (const auto &t : mesh.v) {
        Tri r{remap[t[0]], remap[t[1]], remap[t[2]]};
        std::rotate(r.begin(), std::min_element(r.begin(), r.end()), r.end());
        result.triangles.push_back(r);
    }
    step(.99);
    std::sort(result.triangles.begin(), result.triangles.end());
    report.vertexCount = result.vertices.size();
    report.triangleCount = result.triangles.size();
    step(1.0);
    result.ok = true;
    return result;
}
} // namespace

TerrainTriangulationResult triangulateTerrain(const TerrainTriangulationInput &input,
                                              const TerrainTriangulationOptions &options,
                                              TerrainProgress *progress) {
    try {
        return triangulate(input, options, Steps{progress});
    } catch (const TerrainCancelled &) {
        // Nothing of the run is kept: no vertices, triangles or report.
        TerrainTriangulationResult cancelled;
        cancelled.cancelled = true;
        return cancelled;
    }
}

} // namespace sketchy
