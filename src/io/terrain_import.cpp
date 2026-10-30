#include "io/terrain_import.hpp"
#include "core/terrain_sources.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_drape.hpp"
#include <QJsonArray>
#include <unordered_set>
#include <QJsonDocument>
#include <QLocale>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

namespace sketchy {
namespace {
using Severity = TerrainReport::Severity;
constexpr double xyTolerance = 1e-7; // the adapter's merge tolerance
constexpr double zTolerance = 1e-9;
constexpr double localBound = 1e6;

bool lexLess(const TerrainVec3 &a, const TerrainVec3 &b) {
    return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
}
bool sameXy(const TerrainVec3 &a, const TerrainVec3 &b) {
    return std::abs(a.x - b.x) <= xyTolerance && std::abs(a.y - b.y) <= xyTolerance;
}
bool duplicate(const TerrainVec3 &a, const TerrainVec3 &b) {
    return sameXy(a, b) && std::abs(a.z - b.z) <= zTolerance;
}
int orient(const TerrainVec3 &a, const TerrainVec3 &b, const TerrainVec3 &c) {
    return terrainOrientation({a.x, a.y}, {b.x, b.y}, {c.x, c.y});
}
QString number(double value) { return QString::number(value, 'g', QLocale::FloatingPointShortest); }
std::string text(const QString &value) { return value.toStdString(); }

// A working polyline in the local frame.
struct Line {
    TerrainSourceRole role{TerrainSourceRole::Contour};
    TerrainSourceRef ref;
    std::string reference;
    std::vector<TerrainVec3> points; // a closed ring does not repeat its start
    bool closed{};
};

// Removes zero-length segments (a vertex equal to its predecessor) and collinear spikes (a
// vertex where the polyline doubles back exactly along itself, which the adapter's exact
// constraint check would refuse as an overlap).
void clean(Line &line, std::uint64_t &zeroLength, std::uint64_t &spikes) {
    // R096.c: shared with the source edits (core/terrain_sources.hpp).
    TerrainPolylineCleaning counts;
    cleanTerrainPolyline(line.points, line.closed, counts);
    zeroLength += counts.zeroLength;
    spikes += counts.spikes;
}

// Rings start at their lexicographically smallest vertex and run counter-clockwise; open
// polylines start at their smaller endpoint (core/terrain_sources.hpp, shared with source edits).
void canonical(Line &line) { canonicalTerrainPolyline(line.points, line.closed); }
bool sequenceLess(const Line &a, const Line &b) {
    const auto order = std::lexicographical_compare(a.points.begin(), a.points.end(),
                                                    b.points.begin(), b.points.end(), lexLess);
    if (order)
        return true;
    if (std::lexicographical_compare(b.points.begin(), b.points.end(), a.points.begin(),
                                     a.points.end(), lexLess))
        return false;
    return std::tie(a.closed, a.ref) < std::tie(b.closed, b.ref);
}

// ---- Pre-clipping to the boundary and holes ----------------------------------------------
struct Polygon {
    Line *line{};
    bool hole{};
    // Points to insert per edge: (parameter along the edge, point).
    std::vector<std::vector<std::pair<double, TerrainVec3>>> inserts;
};
struct EdgeRef {
    std::uint32_t polygon, edge;
};
class EdgeGrid {
  public:
    explicit EdgeGrid(const std::vector<Polygon> &polygons) : polygons_(polygons) {
        std::size_t edges = 0;
        minX_ = minY_ = std::numeric_limits<double>::infinity();
        double maxX = -minX_, maxY = -minY_;
        for (const auto &polygon : polygons)
            for (const auto &p : polygon.line->points) {
                minX_ = std::min(minX_, p.x);
                minY_ = std::min(minY_, p.y);
                maxX = std::max(maxX, p.x);
                maxY = std::max(maxY, p.y);
                ++edges;
            }
        if (!edges)
            return;
        for (const auto &polygon : polygons) {
            offsets_.push_back(seen_.size());
            seen_.resize(seen_.size() + polygon.line->points.size(), 0);
        }
        const double extent = std::max({maxX - minX_, maxY - minY_, 1e-6});
        side_ = std::clamp<int>(int(std::sqrt(double(edges))) + 1, 1, 512);
        cell_ = extent / side_ * (1 + 1e-9);
        cells_.resize(std::size_t(side_) * std::size_t(side_));
        for (std::uint32_t k = 0; k < polygons.size(); ++k) {
            const auto &points = polygons[k].line->points;
            for (std::uint32_t e = 0; e < points.size(); ++e) {
                const auto &a = points[e], &b = points[(e + 1) % points.size()];
                visit(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x),
                      std::max(a.y, b.y),
                      [&](std::vector<EdgeRef> &cell) { cell.push_back({k, e}); });
            }
        }
    }
    // Every edge whose cells meet the box, each once.
    template <class F> void query(double x0, double y0, double x1, double y1, F &&f) {
        if (cells_.empty())
            return;
        ++stamp_;
        visit(x0, y0, x1, y1, [&](std::vector<EdgeRef> &cell) {
            for (const auto &ref : cell) {
                auto &seen = seen_[offsets_[ref.polygon] + ref.edge];
                if (seen == stamp_)
                    continue;
                seen = stamp_;
                f(ref);
            }
        });
    }

  private:
    template <class F> void visit(double x0, double y0, double x1, double y1, F &&f) {
        auto clampCell = [&](double v, double min) {
            return std::clamp(int(std::floor((v - min) / cell_)), 0, side_ - 1);
        };
        if (x1 < minX_ - cell_ || y1 < minY_ - cell_ || x0 > minX_ + cell_ * (side_ + 1) ||
            y0 > minY_ + cell_ * (side_ + 1))
            return;
        const int i0 = clampCell(x0, minX_), i1 = clampCell(x1, minX_);
        const int j0 = clampCell(y0, minY_), j1 = clampCell(y1, minY_);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                f(cells_[std::size_t(j) * std::size_t(side_) + std::size_t(i)]);
    }
    const std::vector<Polygon> &polygons_;
    double minX_{}, minY_{}, cell_{1};
    int side_{0};
    std::vector<std::vector<EdgeRef>> cells_;
    std::vector<std::size_t> offsets_;
    std::vector<std::uint64_t> seen_;
    std::uint64_t stamp_{0};
};

bool inBox(const TerrainVec3 &a, const TerrainVec3 &b, const TerrainVec3 &p) {
    return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
           p.y <= std::max(a.y, b.y);
}
double parameter(const TerrainVec3 &a, const TerrainVec3 &b, const TerrainVec3 &p) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double length = dx * dx + dy * dy;
    return length > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.0, 1.0) : 0;
}
TerrainVec3 along(const TerrainVec3 &a, const TerrainVec3 &b, double t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Point in polygon by crossing number with the exact orientation predicate. Clipper2's
// PointInPolygon is not usable on doubles: its CrossProductSign casts the coordinates to
// integers, so points near an edge read as "on" it.
enum class Where { Inside, Outside, On };
Where locate(const TerrainVec3 &m, const std::vector<TerrainVec3> &ring) {
    bool inside = false;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto &a = ring[i], &b = ring[(i + 1) % ring.size()];
        const int o = orient(a, b, m);
        if (o == 0 && inBox(a, b, m))
            return Where::On;
        if ((a.y > m.y) != (b.y > m.y) && (o > 0) == (b.y > a.y))
            inside = !inside;
    }
    return inside ? Where::Inside : Where::Outside;
}
struct ClipCounts {
    std::uint64_t clippedLines{}, piecesKept{}, piecesDropped{}, linesOutside{};
};

// Splits a contour or breakline at every crossing with a boundary or hole edge, at polygon
// vertices that lie on it and at its vertices that lie on a polygon edge; keeps the runs
// inside the region. Crossing points are inserted into the polygon edge too, so the data runs
// exactly to the boundary: the adapter then merges the shared vertex, where a crossing segment
// would otherwise be dropped whole.
std::vector<Line> clip(const Line &line, std::vector<Polygon> &polygons, EdgeGrid &grid,
                       ClipCounts &counts, TerrainReport &report, const TerrainOriginMetres &origin,
                       double scale) {
    auto points = line.points;
    if (line.closed && !points.empty())
        points.push_back(points.front());
    const std::size_t segments = points.size() - 1;
    std::vector<std::vector<std::pair<double, TerrainVec3>>> splits(segments);
    std::vector<bool> vertexSplit(points.size(), false);
    auto insert = [&](const EdgeRef &ref, const TerrainVec3 &p) {
        auto &polygon = polygons[ref.polygon];
        const auto &ring = polygon.line->points;
        const auto &c = ring[ref.edge], &d = ring[(ref.edge + 1) % ring.size()];
        if ((p.x == c.x && p.y == c.y) || (p.x == d.x && p.y == d.y))
            return;
        polygon.inserts[ref.edge].push_back({parameter(c, d, p), p});
    };
    for (std::size_t i = 0; i < segments; ++i) {
        const auto &a = points[i], &b = points[i + 1];
        grid.query(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y),
                   [&](const EdgeRef &ref) {
                       const auto &ring = polygons[ref.polygon].line->points;
                       const auto &c = ring[ref.edge], &d = ring[(ref.edge + 1) % ring.size()];
                       const int o1 = orient(c, d, a), o2 = orient(c, d, b);
                       const int o3 = orient(a, b, c), o4 = orient(a, b, d);
                       if (o1 * o2 < 0 && o3 * o4 < 0) {
                           // Proper crossing.
                           const long double ca =
                               ((long double)d.x - c.x) * ((long double)a.y - c.y) -
                               ((long double)d.y - c.y) * ((long double)a.x - c.x);
                           const long double cb =
                               ((long double)d.x - c.x) * ((long double)b.y - c.y) -
                               ((long double)d.y - c.y) * ((long double)b.x - c.x);
                           const double t = double(ca / (ca - cb));
                           auto p = along(a, b, t);
                           if (sameXy(p, a))
                               p = a;
                           else if (sameXy(p, b))
                               p = b;
                           else if (sameXy(p, c))
                               p = {c.x, c.y, p.z};
                           else if (sameXy(p, d))
                               p = {d.x, d.y, p.z};
                           splits[i].push_back({parameter(a, b, p), p});
                           insert(ref, p);
                           return;
                       }
                       // A polygon vertex inside this segment splits it there.
                       for (const auto *v : {&c, &d})
                           if (orient(a, b, *v) == 0 && inBox(a, b, *v) &&
                               !(v->x == a.x && v->y == a.y) && !(v->x == b.x && v->y == b.y)) {
                               const double t = parameter(a, b, *v);
                               splits[i].push_back({t, {v->x, v->y, along(a, b, t).z}});
                           }
                       // A data vertex on a polygon edge or vertex ends a run there.
                       for (const std::size_t k : {i, i + 1}) {
                           const auto &v = points[k];
                           if ((v.x == c.x && v.y == c.y) || (v.x == d.x && v.y == d.y)) {
                               vertexSplit[k] = true;
                           } else if (orient(c, d, v) == 0 && inBox(c, d, v)) {
                               vertexSplit[k] = true;
                               insert(ref, v);
                           }
                       }
                   });
    }
    // Runs between split points.
    std::vector<std::vector<TerrainVec3>> runs(1);
    std::vector<bool> runEndsAtSplit;
    runs.back().push_back(points.front());
    bool split = false;
    for (std::size_t i = 0; i < segments; ++i) {
        auto &cuts = splits[i];
        std::sort(cuts.begin(), cuts.end(),
                  [](const auto &x, const auto &y) { return x.first < y.first; });
        for (const auto &[t, p] : cuts) {
            if (duplicate(runs.back().back(), p)) {
                if (runs.back().size() > 1) {
                    runs.push_back({runs.back().back()});
                    split = true;
                }
                continue;
            }
            runs.back().push_back(p);
            runs.push_back({p});
            split = true;
        }
        if (!duplicate(runs.back().back(), points[i + 1]))
            runs.back().push_back(points[i + 1]);
        if (vertexSplit[i + 1] && i + 1 < segments && runs.back().size() > 1) {
            runs.push_back({runs.back().back()});
            split = true;
        }
    }
    const bool startSplit = vertexSplit.front();
    auto inside = [&](const std::vector<TerrainVec3> &run) -> std::optional<bool> {
        for (std::size_t i = 0; i + 1 < run.size(); ++i) {
            if (duplicate(run[i], run[i + 1]))
                continue;
            const TerrainVec3 m{(run[i].x + run[i + 1].x) / 2, (run[i].y + run[i + 1].y) / 2, 0};
            for (const auto &polygon : polygons) {
                const auto where = locate(m, polygon.line->points);
                if (where == Where::On)
                    return false;
                if (polygon.hole ? where == Where::Inside : where == Where::Outside)
                    return false;
            }
            return true;
        }
        return std::nullopt; // zero length
    };
    std::vector<std::optional<bool>> keep;
    for (const auto &run : runs)
        keep.push_back(inside(run));
    std::vector<Line> result;
    if (!split) {
        if (keep.front().value_or(false))
            result.push_back(line);
        else {
            ++counts.linesOutside;
            report.locate(
                Severity::Notice, "dropped-outside",
                "Contours and breaklines wholly outside the boundary or inside a hole "
                "were dropped.",
                {line.reference,
                 {},
                 std::array<double, 2>{(line.points.front().x + origin.easting) / scale,
                                       (line.points.front().y + origin.northing) / scale}});
        }
        return result;
    }
    ++counts.clippedLines;
    // A closed ring's first and last runs join at its start unless the start is a split point.
    if (line.closed && runs.size() > 1 && !startSplit && keep.front().value_or(false) &&
        keep.back().value_or(false)) {
        auto &last = runs.back();
        last.insert(last.end(), runs.front().begin() + 1, runs.front().end());
        runs.erase(runs.begin());
        keep.erase(keep.begin());
    }
    for (std::size_t r = 0; r < runs.size(); ++r) {
        if (!keep[r].has_value())
            continue;
        if (!*keep[r]) {
            ++counts.piecesDropped;
            continue;
        }
        ++counts.piecesKept;
        Line piece = line;
        piece.closed = false;
        piece.points = std::move(runs[r]);
        result.push_back(std::move(piece));
    }
    report.locate(Severity::Notice, "clipped",
                  "Contours and breaklines crossing the boundary or a hole were split there; the "
                  "parts outside were dropped.",
                  {line.reference, {}, std::nullopt});
    return result;
}

std::string extentText(const char *axis, double low, double high) {
    return std::string(axis) + " " + text(number(low)) + " .. " + text(number(high)) + " m";
}
} // namespace

std::optional<TerrainImportRole> terrainImportRole(const QString &name) {
    static const std::map<QString, TerrainImportRole> names{
        {"auto", TerrainImportRole::Auto},           {"contour", TerrainImportRole::Contour},
        {"breakline", TerrainImportRole::Breakline}, {"boundary", TerrainImportRole::Boundary},
        {"hole", TerrainImportRole::Hole},           {"ground", TerrainImportRole::Ground},
        {"ignore", TerrainImportRole::Ignore}};
    const auto found = names.find(name);
    return found == names.end() ? std::nullopt : std::optional(found->second);
}
const char *terrainImportRoleName(TerrainImportRole role) {
    switch (role) {
    case TerrainImportRole::Auto:
        return "auto";
    case TerrainImportRole::Contour:
        return "contour";
    case TerrainImportRole::Breakline:
        return "breakline";
    case TerrainImportRole::Boundary:
        return "boundary";
    case TerrainImportRole::Hole:
        return "hole";
    case TerrainImportRole::Ground:
        return "ground";
    case TerrainImportRole::Ignore:
        return "ignore";
    }
    return "auto";
}

TerrainPrepared prepareTerrainSources(const TerrainSourceSet &set,
                                      const TerrainImportOptions &options,
                                      const std::optional<SiteOrigin> &site,
                                      TerrainProgress *progress) {
    const auto step = [progress](double fraction) {
        terrainCheckpoint(progress, TerrainStage::Prepare, fraction);
    };
    step(0);
    TerrainPrepared prepared;
    prepared.report = set.problems;
    auto &report = prepared.report;
    auto &counts = prepared.counts;
    const double scale = set.unit.metresPerUnit;
    // Source extents (every item, before roles) and the proposed origin.
    {
        constexpr double inf = std::numeric_limits<double>::infinity();
        std::array<double, 3> low{inf, inf, inf}, high{-inf, -inf, -inf};
        auto grow = [&](const TerrainVec3 &p) {
            low = {std::min(low[0], p.x), std::min(low[1], p.y), std::min(low[2], p.z)};
            high = {std::max(high[0], p.x), std::max(high[1], p.y), std::max(high[2], p.z)};
        };
        for (const auto &polyline : set.polylines)
            for (const auto &p : polyline.points)
                grow(p);
        for (const auto &point : set.points)
            grow(point.position);
        if (low[0] > high[0])
            low = high = {0, 0, 0};
        prepared.sourceMin = low;
        prepared.sourceMax = high;
        prepared.proposed = {std::round((low[0] + high[0]) / 2 * scale / 100) * 100,
                             std::round((low[1] + high[1]) / 2 * scale / 100) * 100, 0};
        if (prepared.proposed.easting == 0)
            prepared.proposed.easting = 0; // no negative zero in reports
        if (prepared.proposed.northing == 0)
            prepared.proposed.northing = 0;
    }
    if (site) {
        const TerrainOriginMetres document{site->easting, site->northing, site->elevation};
        if (options.origin && !(*options.origin == document))
            throw TerrainError("INVALID_REQUEST",
                               "The document's site origin is (" + text(number(site->easting)) +
                                   " m E, " + text(number(site->northing)) + " m N, " +
                                   text(number(site->elevation)) +
                                   " m); a terrain import cannot use a different origin. "
                                   "Re-anchor the site first, or omit the origin.");
        prepared.origin = document;
        prepared.originSource = "document";
    } else if (options.origin) {
        prepared.origin = *options.origin;
        prepared.originSource = "explicit";
    } else {
        prepared.origin = prepared.proposed;
        prepared.originSource = "proposed";
    }
    const auto origin = prepared.origin;
    for (const double value : {origin.easting, origin.northing, origin.elevation})
        if (!std::isfinite(value) || std::abs(value) > siteCoordinateLimit)
            throw TerrainError("INVALID_REQUEST",
                               "The origin must be finite and within 100,000 km");
    auto local = [&](const TerrainVec3 &p) {
        return TerrainVec3{p.x * scale - origin.easting, p.y * scale - origin.northing,
                           p.z * scale - origin.elevation};
    };
    auto sourceXy = [&](const TerrainVec3 &p) {
        return std::array<double, 2>{(p.x + origin.easting) / scale,
                                     (p.y + origin.northing) / scale};
    };
    auto roleOf = [&](std::uint32_t layer) {
        const auto found = options.roles.find(set.layers.at(layer).name);
        return found == options.roles.end() ? TerrainImportRole::Auto : found->second;
    };
    step(.1);
    // Roles.
    std::vector<Line> contours, breaklines, boundaries, holes;
    struct Ground {
        TerrainVec3 p;
        TerrainSourceRef ref;
        QString code;
    };
    std::vector<Ground> ground;
    std::uint64_t ignored = 0;
    auto place = [&](Line line, TerrainImportRole role, const TerrainVec3 &first) {
        const bool constant =
            std::all_of(line.points.begin(), line.points.end(),
                        [&](const TerrainVec3 &p) { return p.z == line.points.front().z; });
        switch (role) {
        case TerrainImportRole::Auto:
            line.role = constant ? TerrainSourceRole::Contour : TerrainSourceRole::Breakline;
            break;
        case TerrainImportRole::Contour:
            line.role = TerrainSourceRole::Contour;
            if (!constant) {
                line.role = TerrainSourceRole::Breakline;
                report.locate(Severity::Notice, "contour-not-constant",
                              "Contours whose elevation is not constant were imported as "
                              "breaklines.",
                              {line.reference, {}, std::array<double, 2>{first.x, first.y}});
            }
            break;
        case TerrainImportRole::Breakline:
            line.role = TerrainSourceRole::Breakline;
            break;
        case TerrainImportRole::Boundary:
        case TerrainImportRole::Hole:
            line.role = role == TerrainImportRole::Boundary ? TerrainSourceRole::Boundary
                                                            : TerrainSourceRole::Hole;
            if (!line.closed && line.points.size() >= 4 &&
                line.points.front() == line.points.back()) {
                line.points.pop_back();
                line.closed = true;
            }
            if (!line.closed) {
                report.locate(Severity::Refusal, "open-polygon",
                              "Boundaries and holes must be closed polylines.",
                              {line.reference, {}, std::array<double, 2>{first.x, first.y}});
                return;
            }
            break;
        default:
            break;
        }
        (line.role == TerrainSourceRole::Contour     ? contours
         : line.role == TerrainSourceRole::Breakline ? breaklines
         : line.role == TerrainSourceRole::Boundary  ? boundaries
                                                     : holes)
            .push_back(std::move(line));
    };
    for (const auto &polyline : set.polylines) {
        const auto role = roleOf(polyline.layer);
        if (role == TerrainImportRole::Ignore) {
            ++ignored;
            continue;
        }
        if (role == TerrainImportRole::Ground) {
            for (const auto &p : polyline.points)
                ground.push_back({local(p), polyline.ref, set.layers[polyline.layer].name});
            continue;
        }
        Line line;
        line.ref = polyline.ref;
        line.reference = polyline.ref.text();
        line.closed = polyline.closed;
        line.points.reserve(polyline.points.size());
        for (const auto &p : polyline.points)
            line.points.push_back(local(p));
        place(std::move(line), role, polyline.points.front());
    }
    step(.2);
    // Points: ground by default; a code mapped to a line role forms one polyline per code
    // from its points in file order.
    {
        std::map<std::uint32_t, Line> coded;
        std::map<std::uint32_t, TerrainVec3> firstOf;
        for (const auto &point : set.points) {
            const auto role = roleOf(point.layer);
            if (role == TerrainImportRole::Ignore) {
                ++ignored;
                continue;
            }
            if (role == TerrainImportRole::Auto || role == TerrainImportRole::Ground) {
                ground.push_back({local(point.position), point.ref, set.layers[point.layer].name});
                continue;
            }
            auto &line = coded[point.layer];
            if (line.points.empty()) {
                line.ref = point.ref;
                line.reference = "code " + text(set.layers[point.layer].name.isEmpty()
                                                    ? QString("(none)")
                                                    : set.layers[point.layer].name);
                firstOf[point.layer] = point.position;
                line.closed =
                    role == TerrainImportRole::Boundary || role == TerrainImportRole::Hole;
            }
            line.points.push_back(local(point.position));
        }
        for (auto &[layer, line] : coded) {
            if (line.points.size() < 2) {
                report.locate(Severity::Notice, "short-code-line",
                              "Description codes mapped to a line role with fewer than two points "
                              "were dropped.",
                              {line.reference, {}, std::nullopt});
                continue;
            }
            place(std::move(line), roleOf(layer), firstOf[layer]);
        }
    }
    step(.3);
    counts["ignoredByRole"] = ignored;
    if (boundaries.size() > 1)
        for (const auto &boundary : boundaries)
            report.locate(Severity::Refusal, "multiple-boundaries",
                          "A terrain has at most one boundary; choose one layer or code for it.",
                          {boundary.reference, {}, sourceXy(boundary.points.front())});
    // Local extents and the +-1e6 m bound.
    {
        constexpr double inf = std::numeric_limits<double>::infinity();
        std::array<double, 3> low{inf, inf, inf}, high{-inf, -inf, -inf};
        auto grow = [&](const TerrainVec3 &p) {
            low = {std::min(low[0], p.x), std::min(low[1], p.y), std::min(low[2], p.z)};
            high = {std::max(high[0], p.x), std::max(high[1], p.y), std::max(high[2], p.z)};
        };
        for (const auto *lines : {&contours, &breaklines, &boundaries, &holes})
            for (const auto &line : *lines)
                for (const auto &p : line.points)
                    grow(p);
        for (const auto &g : ground)
            grow(g.p);
        if (low[0] > high[0])
            low = high = {0, 0, 0};
        prepared.localMin = low;
        prepared.localMax = high;
        const char *axes[3]{"E", "N", "Z"};
        for (int a = 0; a < 3; ++a)
            if (std::abs(low[a]) > localBound || std::abs(high[a]) > localBound)
                report.locate(Severity::Refusal, "extent-exceeded",
                              "Data would land beyond +-1,000,000 m of the origin; choose an "
                              "origin nearer the data.",
                              {"local extent",
                               {},
                               std::nullopt,
                               extentText(axes[a], low[a], high[a]) + " exceeds +-1,000,000 m"});
    }
    if (report.refused())
        return prepared;
    step(.35);
    // Clean, canonical orientation, then clip.
    std::uint64_t zeroLength = 0, spikes = 0;
    std::vector<Line> lines;
    for (auto *group : {&contours, &breaklines, &boundaries, &holes}) {
        for (auto &line : *group) {
            clean(line, zeroLength, spikes);
            const bool polygon =
                line.role == TerrainSourceRole::Boundary || line.role == TerrainSourceRole::Hole;
            if (line.points.size() < (polygon ? 3u : 2u)) {
                if (polygon)
                    report.locate(Severity::Refusal, "invalid-polygon",
                                  "Boundaries and holes need three distinct vertices.",
                                  {line.reference, {}, std::nullopt});
                else
                    report.locate(Severity::Notice, "degenerate-polylines",
                                  "Polylines with no length after cleaning were dropped.",
                                  {line.reference, {}, std::nullopt});
                line.points.clear();
                continue;
            }
            if (line.closed && line.points.size() < 3)
                line.closed = false;
            canonical(line);
        }
        std::erase_if(*group, [](const Line &line) { return line.points.empty(); });
    }
    if (zeroLength)
        report.add(Severity::Notice, "zero-length-segments", zeroLength,
                   "Zero-length segments (repeated vertices) were dropped.");
    if (spikes)
        report.add(Severity::Notice, "collinear-spikes", spikes,
                   "Collinear spikes (vertices where a polyline doubles back on itself) were "
                   "dropped.");
    counts["zeroLengthSegments"] = zeroLength;
    counts["collinearSpikes"] = spikes;
    if (report.refused())
        return prepared;
    step(.45);
    ClipCounts clip_;
    if (!boundaries.empty() || !holes.empty()) {
        std::vector<Polygon> polygons;
        for (auto *group : {&boundaries, &holes})
            for (auto &line : *group) {
                Polygon polygon;
                polygon.line = &line;
                polygon.hole = line.role == TerrainSourceRole::Hole;
                polygon.inserts.resize(line.points.size());
                polygons.push_back(std::move(polygon));
            }
        EdgeGrid grid(polygons);
        for (auto *group : {&contours, &breaklines}) {
            std::vector<Line> kept;
            for (const auto &line : *group)
                for (auto &piece : clip(line, polygons, grid, clip_, report, origin, scale))
                    kept.push_back(std::move(piece));
            *group = std::move(kept);
        }
        for (auto &polygon : polygons) {
            auto &ring = polygon.line->points;
            std::vector<TerrainVec3> out;
            for (std::size_t e = 0; e < ring.size(); ++e) {
                out.push_back(ring[e]);
                auto &inserts = polygon.inserts[e];
                std::sort(inserts.begin(), inserts.end(), [](const auto &x, const auto &y) {
                    return std::tie(x.first, x.second.x, x.second.y) <
                           std::tie(y.first, y.second.x, y.second.y);
                });
                for (const auto &[u, p] : inserts)
                    if (!(p.x == out.back().x && p.y == out.back().y))
                        out.push_back(p);
            }
            while (out.size() > 1 && out.back().x == out.front().x && out.back().y == out.front().y)
                out.pop_back();
            ring = std::move(out);
        }
        for (auto *group : {&contours, &breaklines, &boundaries, &holes})
            for (auto &line : *group)
                canonical(line);
    }
    counts["clippedPolylines"] = clip_.clippedLines;
    counts["clippedPiecesKept"] = clip_.piecesKept;
    counts["clippedPiecesDropped"] = clip_.piecesDropped;
    counts["droppedOutsidePolylines"] = clip_.linesOutside;
    step(.6);
    // Canonical order: polylines by vertex sequence within each role (ties by reference),
    // points by (x, y, z) (ties by reference), grouped by description code.
    for (auto *group : {&contours, &breaklines, &boundaries, &holes})
        std::sort(group->begin(), group->end(), sequenceLess);
    step(.65);
    std::sort(ground.begin(), ground.end(), [](const Ground &a, const Ground &b) {
        if (lexLess(a.p, b.p))
            return true;
        if (lexLess(b.p, a.p))
            return false;
        return std::tie(a.ref, a.code) < std::tie(b.ref, b.code);
    });
    step(.75);
    // Records.
    std::size_t vertices = 0;
    auto record = [&](const Line &line) {
        TerrainSource source;
        source.role = line.role;
        source.reference = line.reference;
        source.points.reserve(line.points.size() + 1);
        for (const auto &p : line.points)
            source.points.push_back({p.x, p.y, p.z});
        if (line.closed &&
            (line.role == TerrainSourceRole::Contour || line.role == TerrainSourceRole::Breakline))
            source.points.push_back(source.points.front());
        vertices += source.points.size();
        prepared.sources.push_back(std::move(source));
    };
    for (const auto *group : {&contours, &breaklines, &boundaries, &holes})
        for (const auto &line : *group)
            record(line);
    {
        std::map<QString, std::vector<const Ground *>> byCode;
        for (const auto &g : ground)
            byCode[g.code].push_back(&g);
        step(.8);
        for (const auto &[code, members] : byCode) {
            TerrainSource source;
            source.role = TerrainSourceRole::Points;
            source.reference =
                "points, code " + (code.isEmpty() ? std::string("(none)") : text(code));
            for (std::size_t k = 0; k < members.size(); ++k) {
                if (progress && (k & 0x3fff) == 0)
                    step(.8 + .2 * double(k) / double(members.size()));
                const auto *g = members[k];
                source.points.push_back({g->p.x, g->p.y, g->p.z});
                auto &refs = prepared.pointRefs[{g->p.x, g->p.y}];
                if (refs.size() < 2)
                    refs.push_back(g->ref);
            }
            vertices += source.points.size();
            prepared.sources.push_back(std::move(source));
        }
    }
    prepared.roles = {{"contour", contours.size()},
                      {"breakline", breaklines.size()},
                      {"boundary", boundaries.size()},
                      {"hole", holes.size()},
                      {"points", ground.size()}};
    if (prepared.sources.size() > TerrainLimits::sources ||
        vertices > TerrainLimits::sourceVertices)
        report.locate(Severity::Refusal, "limit-exceeded",
                      "A terrain stores at most 100,000 sources and 250,000 source vertices.",
                      {"sources",
                       {},
                       std::nullopt,
                       std::to_string(prepared.sources.size()) + " sources, " +
                           std::to_string(vertices) + " vertices"});
    // R096.c: a stored -0.0 would not survive the record's JSON (it reads back as +0.0), and the
    // mesh triangulated from it would then differ in its bits from the one Regenerate builds after
    // a reopen (C7). Source coordinates are stored as +0.0 instead; the value is the same.
    for (auto &source : prepared.sources) {
        bool negativeZero = false;
        for (const auto &p : source.points)
            negativeZero = negativeZero || (p.x == 0 && std::signbit(p.x)) ||
                           (p.y == 0 && std::signbit(p.y)) || (p.z == 0 && std::signbit(p.z));
        if (negativeZero)
            for (auto &p : source.points)
                p = {p.x + 0.0, p.y + 0.0, p.z + 0.0};
    }
    prepared.ok = !report.refused();
    return prepared;
}

TerrainTriangulationInput terrainTriangulationInput(const std::vector<TerrainSource> &sources) {
    TerrainTriangulationInput input;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto &source = sources[i];
        const std::uint64_t id = i + 1;
        auto points = source.points;
        const bool closed = points.size() >= 4 && points.front() == points.back() &&
                            (source.role == TerrainSourceRole::Contour ||
                             source.role == TerrainSourceRole::Breakline);
        if (closed)
            points.pop_back();
        switch (source.role) {
        case TerrainSourceRole::Contour: {
            TerrainContour contour;
            contour.z = points.empty() ? 0 : points.front().z;
            for (const auto &p : points)
                contour.points.push_back({p.x, p.y});
            contour.closed = closed;
            contour.sourceId = id;
            input.contours.push_back(std::move(contour));
            break;
        }
        case TerrainSourceRole::Breakline: {
            TerrainBreakline breakline;
            for (const auto &p : points)
                breakline.points.push_back({p.x, p.y, p.z});
            breakline.closed = closed;
            breakline.sourceId = id;
            input.breaklines.push_back(std::move(breakline));
            break;
        }
        case TerrainSourceRole::Boundary:
        case TerrainSourceRole::Hole: {
            TerrainPolygon polygon;
            for (const auto &p : points)
                polygon.points.push_back({p.x, p.y});
            polygon.sourceId = id;
            if (source.role == TerrainSourceRole::Hole)
                input.holes.push_back(std::move(polygon));
            else if (!input.boundary)
                input.boundary = std::move(polygon);
            break;
        }
        case TerrainSourceRole::Points:
            for (const auto &p : points)
                input.points.push_back({{p.x, p.y, p.z}, id});
            break;
        }
    }
    return input;
}

namespace {
// The triangulation stage shared by import, regenerate and set_parameters: stored sources
// to a mesh, with the adapter's refusals and notices added to `report` (references by
// `reference`, XY mapped by `toSource`) and its counts added to `counts`.
struct MeshStage {
    TerrainMesh mesh;
    std::uint64_t triangles{}, vertices{};
    std::size_t tiles{};
    QJsonObject flat;
};
MeshStage triangulateSources(
    const std::vector<TerrainSource> &sources, const TerrainTriangulationOptions &options,
    bool useBoundary, std::size_t otherTerrainTriangles, TerrainReport &report, QJsonObject &counts,
    const std::function<std::string(std::uint64_t, const TerrainVec2 &, int)> &reference,
    const std::function<std::array<double, 2>(const TerrainVec2 &)> &toSource,
    TerrainProgress *progress = nullptr) {
    MeshStage stage;
    auto input = terrainTriangulationInput(sources);
    if (!useBoundary)
        input.boundary.reset();
    const auto triangulated = triangulateTerrain(input, options, progress);
    if (triangulated.cancelled)
        throw TerrainCancelled();
    const auto &r = triangulated.report;
    for (const auto &problem : r.refusals) {
        const std::string kind = terrainProblemKindName(problem.kind);
        report.add(Severity::Refusal, kind, problem.count, problem.message);
        for (const auto &location : problem.locations)
            report.locate(
                Severity::Refusal, kind, problem.message,
                {reference(location.sourceA, location.xy, 0),
                 location.sourceB ? reference(location.sourceB, location.xy, 1) : std::string(),
                 toSource(location.xy)},
                false);
    }
    auto notice = [&](const char *kind, std::uint64_t count, const char *message) {
        if (count)
            report.add(Severity::Notice, kind, count, message);
    };
    notice("merged-duplicates", r.mergedDuplicates,
           "Exact duplicate points and vertices were merged.");
    notice("merged-segments", r.mergedSegments, "Repeated segments were merged.");
    notice("zero-length-segments", r.droppedZeroLengthSegments,
           "Zero-length segments (repeated vertices) were dropped.");
    notice("dropped-outside", r.droppedOutsideVertices,
           "Points and vertices outside the boundary or inside a hole were dropped.");
    notice("dropped-holes", r.droppedHoles, "Holes entirely outside the boundary were dropped.");
    notice("extrapolated-polygon-vertices", r.extrapolatedPolygonVertices,
           "Boundary or hole vertices outside the data took the nearest source elevation.");
    if (r.flatTrianglesRemaining) {
        report.add(Severity::Notice, "flat-triangles", r.flatTrianglesRemaining,
                   "Flat triangles remain (three vertices of one contour); add spot heights "
                   "or a breakline to resolve them.");
        for (const auto &centroid : r.flatLocations)
            report.locate(Severity::Notice, "flat-triangles", "",
                          {"centroid", {}, toSource(centroid)}, false);
    }
    counts["rawVertices"] = double(r.rawVertexCount);
    counts["uniqueVertices"] = double(r.uniqueVertexCount);
    counts["mergedDuplicates"] = double(r.mergedDuplicates);
    counts["mergedSegments"] = double(r.mergedSegments);
    counts["droppedZeroLengthSegments"] = double(r.droppedZeroLengthSegments);
    counts["droppedOutsideVertices"] = double(r.droppedOutsideVertices);
    counts["droppedOutsideSegments"] = double(r.droppedOutsideSegments);
    counts["droppedHoles"] = double(r.droppedHoles);
    counts["interpolatedPolygonVertices"] = double(r.interpolatedPolygonVertices);
    counts["extrapolatedPolygonVertices"] = double(r.extrapolatedPolygonVertices);
    counts["constraintEdges"] = double(r.constraintEdges);
    counts["trimmedTriangles"] = double(r.trimmedTriangles);
    counts["unusedVertices"] = double(r.unusedVertices);
    stage.flat = {{"initial", double(r.flatTrianglesInitial)},
                  {"flips", double(r.flipsApplied)},
                  {"steinerPoints", double(r.steinerPointsAdded)},
                  {"remaining", double(r.flatTrianglesRemaining)},
                  {"areaRemaining", r.flatAreaRemaining}};
    if (!triangulated.ok || report.refused())
        return stage;
    // R095.c2: the document limit counts every terrain; `otherTerrainTriangles` are those of
    // the other terrains (zero for import, where the edit itself checks the limit).
    if (otherTerrainTriangles &&
        otherTerrainTriangles + r.triangleCount > TerrainLimits::triangles) {
        report.locate(Severity::Refusal, "triangle-budget",
                      "The document's terrains would exceed the terrain triangle limit.",
                      {"document",
                       {},
                       std::nullopt,
                       "terrain triangles would be " +
                           std::to_string(otherTerrainTriangles + r.triangleCount) + " of " +
                           std::to_string(TerrainLimits::triangles)});
        return stage;
    }
    stage.mesh.vertices.reserve(triangulated.vertices.size());
    for (const auto &v : triangulated.vertices)
        stage.mesh.vertices.push_back({v.x, v.y, v.z});
    for (const auto &t : triangulated.triangles)
        stage.mesh.triangles.push_back(t);
    // The document tessellates every tile face on a 1e-7 m grid and refuses a face
    // whose area is below its tolerance. The adapter never returns such a triangle for
    // valid input, but refuse it here, located, rather than hand the kernel one.
    for (const auto &t : stage.mesh.triangles) {
        const auto &a = stage.mesh.vertices[t[0]], &b = stage.mesh.vertices[t[1]],
                   &c = stage.mesh.vertices[t[2]];
        if (length(cross(b - a, c - a)) < 1e-12)
            report.locate(
                Severity::Refusal, "degenerate-triangle",
                "Triangles below the modelling tolerance (area under 5e-13 "
                "m\u00b2) cannot become faces.",
                {"centroid", {}, toSource({(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3})});
    }
    if (report.refused()) {
        stage.mesh = {};
    } else {
        stage.triangles = r.triangleCount;
        stage.vertices = r.vertexCount;
        stage.tiles = tileTerrainTriangles(stage.mesh, progress).tiles.size();
    }
    return stage;
}
} // namespace
TerrainTriangulationOptions terrainTriangulationOptions(const TerrainParameters &parameters) {
    TerrainTriangulationOptions options;
    options.flatMode =
        parameters.flipFlatTriangles ? TerrainFlatMode::Flip : TerrainFlatMode::ReportOnly;
    options.steinerFill = parameters.steinerFill;
    const bool trim = parameters.boundary == TerrainBoundaryMode::MaxEdgeLength ||
                      parameters.boundary == TerrainBoundaryMode::Boundary;
    if (trim && parameters.maxEdgeLength > 0)
        options.maxEdgeLength = parameters.maxEdgeLength;
    return options;
}
namespace {
QJsonObject parametersJson(const TerrainParameters &parameters) {
    return {{"boundary", parameters.boundary == TerrainBoundaryMode::Boundary ? "boundary"
                         : parameters.boundary == TerrainBoundaryMode::MaxEdgeLength
                             ? "maxEdgeLength"
                             : "convexHull"},
            {"maxEdgeLength", parameters.maxEdgeLength},
            {"flipFlatTriangles", parameters.flipFlatTriangles},
            {"steinerFill", parameters.steinerFill}};
}
// The refusal code and message from a finished report's refusals.
void refusalOutcome(const QJsonObject &terrain, const std::string &prefix, std::string &code,
                    std::string &message) {
    bool limit = false;
    std::string kinds;
    for (const auto &value : terrain["refusals"].toArray()) {
        const auto entry = value.toObject();
        const auto kind = entry["kind"].toString();
        limit = limit || kind == "triangle-budget" || kind == "extent-exceeded" ||
                kind == "limit-exceeded";
        if (!kinds.empty())
            kinds += ", ";
        kinds += text(kind) + " (" + std::to_string(qint64(entry["count"].toDouble())) + ")";
    }
    code = limit ? "LIMIT_EXCEEDED" : "INVALID_REQUEST";
    message = (prefix + " refused; nothing was changed: " +
               (kinds.empty() ? std::string("no terrain") : kinds))
                  .substr(0, 256);
}
} // namespace

namespace {
TerrainImportResult importDryRun(const TerrainSourceSet &set, const TerrainImportOptions &options,
                                 const std::optional<SiteOrigin> &site,
                                 const TerrainImportProvenance &provenance,
                                 TerrainProgress *progress) {
    auto prepared = prepareTerrainSources(set, options, site, progress);
    auto &report = prepared.report;
    const double scale = set.unit.metresPerUnit;
    const auto origin = prepared.origin;
    TerrainImportResult result;
    result.origin = origin;
    result.proposed = prepared.proposed;
    result.originSource = prepared.originSource;
    QJsonObject counts;
    for (const auto &[key, value] : prepared.counts)
        counts[key] = double(value);
    QJsonObject flat, roles;
    for (const auto &[key, value] : prepared.roles)
        roles[key] = double(value);
    std::uint64_t triangles = 0, meshVertices = 0;
    const bool boundary = std::any_of(
        prepared.sources.begin(), prepared.sources.end(),
        [](const TerrainSource &source) { return source.role == TerrainSourceRole::Boundary; });
    if (prepared.ok) {
        auto reference = [&](std::uint64_t id, const TerrainVec2 &xy, int which) -> std::string {
            if (!id || id > prepared.sources.size())
                return {};
            const auto &source = prepared.sources[id - 1];
            if (source.role == TerrainSourceRole::Points) {
                const auto found = prepared.pointRefs.find({xy.x, xy.y});
                if (found != prepared.pointRefs.end() && !found->second.empty())
                    return found
                        ->second[std::min<std::size_t>(std::size_t(which),
                                                       found->second.size() - 1)]
                        .text();
            }
            return source.reference;
        };
        auto toSource = [&](const TerrainVec2 &xy) {
            return std::array<double, 2>{(xy.x + origin.easting) / scale,
                                         (xy.y + origin.northing) / scale};
        };
        auto stage = triangulateSources(prepared.sources, options.triangulation, true,
                                        options.otherTerrainTriangles, report, counts, reference,
                                        toSource, progress);
        flat = stage.flat;
        result.mesh = std::move(stage.mesh);
        triangles = stage.triangles;
        meshVertices = stage.vertices;
        result.tiles = stage.tiles;
    }
    result.ok = prepared.ok && !report.refused() && !result.mesh.triangles.empty();
    if (prepared.ok && !result.ok && !report.refused())
        report.add(Severity::Refusal, "triangulation-failed", 1,
                   "The sources gave no terrain triangles.");
    // The record.
    auto &record = result.record;
    record.sources = std::move(prepared.sources);
    assignTerrainSourceIds(record); // R096.a: ids 1..n in canonical order.
    record.parameters.boundary = boundary ? TerrainBoundaryMode::Boundary
                                 : options.triangulation.maxEdgeLength
                                     ? TerrainBoundaryMode::MaxEdgeLength
                                     : TerrainBoundaryMode::ConvexHull;
    record.parameters.maxEdgeLength = options.triangulation.maxEdgeLength.value_or(0);
    record.parameters.flipFlatTriangles = options.triangulation.flatMode == TerrainFlatMode::Flip;
    record.parameters.steinerFill = options.triangulation.steinerFill;
    record.generator = terrainGenerator;
    record.provenance = {text(set.sha256), text(set.unit.name), scale};
    if (!site)
        result.establish = SiteOrigin{origin.easting,
                                      origin.northing,
                                      origin.elevation,
                                      provenance.crs,
                                      {provenance.sourceName, text(set.sha256)}, std::nullopt};
    // The report.
    const QString outcome = !result.ok            ? "refused"
                            : report.hasNotices() ? "importedWithNotices"
                                                  : "imported";
    auto originJson = [](const TerrainOriginMetres &o) {
        return QJsonObject{
            {"easting", o.easting}, {"northing", o.northing}, {"elevation", o.elevation}};
    };
    const QString formula = "model = (source × " + set.unit.exact + " m/" + set.unit.name +
                            ") − (" + number(origin.easting) + " m E, " + number(origin.northing) +
                            " m N, " + number(origin.elevation) + " m)";
    auto triple = [](const std::array<double, 3> &v) { return QJsonArray{v[0], v[1], v[2]}; };
    QJsonObject envelope{{"outcome", outcome},
                         {"units", set.unit.name},
                         {"metresPerUnit", scale},
                         {"metresPerUnitExact", set.unit.exact},
                         {"conversion", formula},
                         {"sourceExtents", QJsonObject{{"min", triple(prepared.sourceMin)},
                                                       {"max", triple(prepared.sourceMax)},
                                                       {"units", set.unit.name}}},
                         {"localExtents", QJsonObject{{"min", triple(prepared.localMin)},
                                                      {"max", triple(prepared.localMax)},
                                                      {"units", "m"}}},
                         {"proposedOrigin", originJson(prepared.proposed)},
                         {"origin", originJson(origin)},
                         {"originSource", prepared.originSource},
                         {"roles", roles},
                         {"counts", counts},
                         {"flatTriangles", flat},
                         {"triangles", double(triangles)},
                         {"vertices", double(meshVertices)},
                         {"tiles", double(result.tiles)},
                         {"generator", terrainGenerator},
                         {"parameters", parametersJson(record.parameters)}};
    if (!provenance.crs.empty())
        envelope["crs"] = QString::fromStdString(provenance.crs);
    if (site && !provenance.crs.empty() && provenance.crs != site->crs)
        report.add(Severity::Notice, "crs-differs", 1,
                   "The CRS label differs from the document's; the document's origin and label "
                   "are kept.");
    result.report = set.report;
    const auto sourceBytes =
        std::size_t(QJsonDocument(set.report).toJson(QJsonDocument::Compact).size());
    const auto budget = TerrainReportBounds::reportBytes > sourceBytes + 64
                            ? TerrainReportBounds::reportBytes - sourceBytes - 64
                            : TerrainReportBounds::reportBytes / 2;
    result.report["terrain"] = report.json(envelope, budget);
    if (!result.ok)
        refusalOutcome(result.report["terrain"].toObject(), "Terrain import", result.code,
                       result.message);
    terrainCheckpoint(progress, TerrainStage::Tile, 1);
    return result;
}
} // namespace

TerrainImportResult runTerrainImport(const TerrainSourceSet &set,
                                     const TerrainImportOptions &options,
                                     const std::optional<SiteOrigin> &site,
                                     const TerrainImportProvenance &provenance,
                                     TerrainProgress *progress) {
    try {
        return importDryRun(set, options, site, provenance, progress);
    } catch (const TerrainCancelled &) {
        TerrainImportResult cancelled;
        cancelled.cancelled = true;
        cancelled.code = "CANCELLED";
        cancelled.message = "Terrain import cancelled; nothing was changed";
        return cancelled;
    }
}

QJsonObject terrainSourceInspection(const TerrainSourceSet &set,
                                    const TerrainImportResult &dryRun) {
    QJsonArray layers;
    for (const auto &layer : set.layers) {
        const QString role = layer.points && !layer.polylines ? "ground"
                             : !layer.polylines               ? "none"
                             : !layer.varyingZ                ? "contour"
                             : !layer.constantZ               ? "breakline"
                                                              : "contour or breakline by Z";
        layers.append(QJsonObject{{"name", layer.name},
                                  {"polylines", double(layer.polylines)},
                                  {"closed", double(layer.closed)},
                                  {"constantZ", double(layer.constantZ)},
                                  {"varyingZ", double(layer.varyingZ)},
                                  {"points", double(layer.points)},
                                  {"defaultRole", role}});
    }
    const auto terrain = dryRun.report["terrain"].toObject();
    return {{"apiVersion", 1},
            {"format", set.format},
            {set.format == "dxf" ? "layers" : "codes", layers},
            {"sourceExtents", terrain["sourceExtents"]},
            {"localExtents", terrain["localExtents"]},
            {"proposedOrigin", terrain["proposedOrigin"]},
            {"origin", terrain["origin"]},
            {"originSource", terrain["originSource"]},
            {"conversion", terrain["conversion"]},
            {"report", dryRun.report}};
}

// ---- R095.c2 ------------------------------------------------------------------------------

void validateTerrainParameters(const TerrainParameters &parameters,
                               const std::vector<TerrainSource> &sources) {
    if (!std::isfinite(parameters.maxEdgeLength) || parameters.maxEdgeLength < 0 ||
        parameters.maxEdgeLength > 2e6 ||
        (parameters.maxEdgeLength > 0 && parameters.maxEdgeLength < 1e-3))
        throw TerrainError("INVALID_REQUEST",
                           "maxEdgeLength is 0 (not set) or 0.001 to 2,000,000 m");
    if (parameters.boundary == TerrainBoundaryMode::Boundary &&
        std::none_of(sources.begin(), sources.end(), [](const TerrainSource &source) {
            return source.role == TerrainSourceRole::Boundary;
        }))
        throw TerrainError("INVALID_REQUEST",
                           "Boundary mode needs a boundary source; this terrain has none");
    if (parameters.boundary == TerrainBoundaryMode::MaxEdgeLength &&
        !(parameters.maxEdgeLength > 0))
        throw TerrainError("INVALID_REQUEST",
                           "Max edge length mode needs a positive maxEdgeLength");
}

namespace {
TerrainImportResult regenerateDryRun(const TerrainRecord &record,
                                     const TerrainParameters &parameters,
                                     std::size_t otherTerrainTriangles, TerrainReport report,
                                     TerrainProgress *progress, std::uint32_t required) {
    validateTerrainParameters(parameters, record.sources);
    TerrainImportResult result;
    QJsonObject counts, roles;
    for (const auto &source : record.sources) {
        const char *role = source.role == TerrainSourceRole::Contour     ? "contour"
                           : source.role == TerrainSourceRole::Breakline ? "breakline"
                           : source.role == TerrainSourceRole::Boundary  ? "boundary"
                           : source.role == TerrainSourceRole::Hole      ? "hole"
                                                                         : "points";
        roles[role] = roles[role].toDouble() +
                      double(source.role == TerrainSourceRole::Points ? source.points.size() : 1);
    }
    for (const char *role : {"contour", "breakline", "boundary", "hole", "points"})
        if (!roles.contains(role))
            roles[role] = 0;
    auto reference = [&](std::uint64_t id, const TerrainVec2 &, int) -> std::string {
        return id && id <= record.sources.size() ? record.sources[id - 1].reference : std::string();
    };
    auto local = [](const TerrainVec2 &xy) { return std::array<double, 2>{xy.x, xy.y}; };
    // With edits the budget is checked on the replayed tiles below, not on the base mesh.
    auto stage = triangulateSources(record.sources, terrainTriangulationOptions(parameters),
                                    parameters.boundary == TerrainBoundaryMode::Boundary,
                                    record.edits.empty() ? otherTerrainTriangles : 0, report,
                                    counts, reference, local, progress);
    result.mesh = std::move(stage.mesh);
    result.tiles = stage.tiles;
    result.ok = !report.refused() && !result.mesh.triangles.empty();
    if (!result.ok && !report.refused())
        report.add(Severity::Refusal, "triangulation-failed", 1,
                   "The sources gave no terrain triangles.");
    // R096.a: the base tiling, then the replay of the edit list (ADR 0169 §2).
    QJsonObject edits{{"count", double(record.edits.size())}, {"applied", 0}, {"skipped", 0}};
    std::vector<TerrainEditPtr> replayedEdits;
    if (result.ok) {
        const auto base = terrainTileSet(result.mesh, progress);
        auto replayed =
            replayTerrainEdits(base, record.edits, progress, record.sources, parameters);
        std::size_t triangles = 0;
        for (const auto &tile : replayed.tiles.tiles)
            triangles += tile->triangles.size();
        for (const auto &skip : replayed.skipped)
            result.skipped.emplace_back(skip.edit, skip.notice);
        for (const auto &skip : replayed.skipped)
            report.locate(Severity::Notice, "edit-skipped",
                          "Recorded terrain changes that no longer apply were skipped and kept "
                          "in the list.",
                          {"edit " + std::to_string(skip.edit) + " (" + terrainQuote(skip.kind) +
                               ")",
                           {},
                           std::nullopt,
                           terrainQuote(skip.notice)});
        // R096.d: an entry the command requires (terrain.update_edit) must apply.
        for (const auto &skip : replayed.skipped) {
            if (!required || skip.edit != required)
                continue;
            result.ok = false;
            const std::string reference = "edit " + std::to_string(skip.edit);
            bool located = false;
            try {
                if (skip.error)
                    std::rethrow_exception(skip.error);
            } catch (const TerrainEditRefused &error) {
                for (const auto &problem : error.problems()) {
                    report.add(Severity::Refusal, problem.kind, problem.count, problem.message);
                    for (const auto &location : problem.locations)
                        report.locate(Severity::Refusal, problem.kind, problem.message,
                                      {reference,
                                       location.sourceB ? "change " + std::to_string(location.sourceB)
                                                        : std::string(),
                                       std::array<double, 2>{location.xy.x, location.xy.y}},
                                      false);
                    located = true;
                }
            } catch (...) {
            }
            if (!located)
                report.locate(Severity::Refusal, "edit-refused",
                              "The changed entry does not apply.",
                              {reference, {}, std::nullopt, terrainQuote(skip.notice)});
        }
        edits["applied"] = double(replayed.applied);
        edits["skipped"] = double(replayed.skipped.size());
        if (!record.edits.empty()) {
            if (otherTerrainTriangles + triangles > TerrainLimits::triangles) {
                report.locate(Severity::Refusal, "triangle-budget",
                              "The document's terrains would exceed the terrain triangle limit.",
                              {"document",
                               {},
                               std::nullopt,
                               "terrain triangles would be " +
                                   std::to_string(otherTerrainTriangles + triangles) + " of " +
                                   std::to_string(TerrainLimits::triangles)});
                result.ok = false;
            }
            std::unordered_set<TerrainPositionKey, TerrainPositionHash> positions;
            for (const auto &tile : replayed.tiles.tiles)
                for (const auto &p : tile->vertices)
                    positions.insert(TerrainPositionKey::of(p));
            stage.triangles = triangles;
            stage.vertices = positions.size();
            result.tiles = replayed.tiles.tiles.size();
        }
        // R096.d: entries whose kind recomputed their resolved geometry (a pad's daylight line
        // on a changed base) or cleared it (skipped) are stored as replayed.
        if (!record.edits.empty()) {
            replayedEdits = std::move(replayed.edits);
            if (terrainEditBytes(replayedEdits) > TerrainLimits::editBytes) {
                report.locate(Severity::Refusal, "limit-exceeded",
                              "The replayed edit list would exceed 8 MiB.",
                              {"edits", {}, std::nullopt,
                               "use Reset edits, or Explode terrain to keep the surface as "
                               "ordinary geometry"});
                result.ok = false;
            }
        }
        if (result.ok)
            result.tileSet = std::move(replayed.tiles);
    }
    result.record = record;
    if (!replayedEdits.empty())
        result.record.edits = std::move(replayedEdits);
    result.record.parameters = parameters;
    result.record.generator = terrainGenerator;
    // R096.e: the region paint of the replayed tiles, here on the run (with its progress and the
    // paint work bound) rather than while the tiles are applied.
    if (result.ok && result.tileSet) {
        try {
            paintTerrainTileSet(*result.tileSet, result.record, progress);
        } catch (const TerrainEditRefused &error) {
            for (const auto &problem : error.problems()) {
                report.add(Severity::Refusal, problem.kind, problem.count, problem.message);
                for (const auto &location : problem.locations)
                    report.locate(Severity::Refusal, problem.kind, problem.message,
                                  {"paint", {}, std::array<double, 2>{location.xy.x, location.xy.y}},
                                  false);
            }
            result.ok = false;
            result.tileSet.reset();
        }
    }
    const QString outcome = !result.ok            ? "refused"
                            : report.hasNotices() ? "regeneratedWithNotices"
                                                  : "regenerated";
    QJsonObject envelope{{"outcome", outcome},
                         {"units", "m"},
                         {"space", "terrain"},
                         {"roles", roles},
                         {"counts", counts},
                         {"flatTriangles", stage.flat},
                         {"triangles", double(stage.triangles)},
                         {"vertices", double(stage.vertices)},
                         {"tiles", double(result.tiles)},
                         {"generator", terrainGenerator},
                         {"parameters", parametersJson(parameters)},
                         {"edits", edits}};
    result.report = {{"apiVersion", 1},
                     {"format", "terrain-record"},
                     {"units", "m"},
                     {"metresPerUnit", 1},
                     {"metresPerUnitExact", "1"},
                     {"sources", double(record.sources.size())}};
    result.report["terrain"] = report.json(envelope, TerrainReportBounds::reportBytes - 512);
    if (!result.ok)
        refusalOutcome(result.report["terrain"].toObject(),
                       required ? "Edit terrain change" : "Terrain regenerate", result.code,
                       result.message);
    terrainCheckpoint(progress, TerrainStage::Replay, 1);
    return result;
}
} // namespace

TerrainImportResult runTerrainRegenerate(const TerrainRecord &record,
                                         const TerrainParameters &parameters,
                                         std::size_t otherTerrainTriangles, TerrainReport report,
                                         TerrainProgress *progress, std::uint32_t required) {
    try {
        return regenerateDryRun(record, parameters, otherTerrainTriangles, std::move(report),
                                progress, required);
    } catch (const TerrainCancelled &) {
        TerrainImportResult cancelled;
        cancelled.cancelled = true;
        cancelled.code = "CANCELLED";
        cancelled.message = "Terrain regeneration cancelled; nothing was changed";
        return cancelled;
    }
}

std::map<QString, TerrainImportRole> terrainEdgeRoles() {
    return {{"edges", TerrainImportRole::Auto},
            {"contour", TerrainImportRole::Contour},
            {"breakline", TerrainImportRole::Breakline},
            {"boundary", TerrainImportRole::Boundary},
            {"hole", TerrainImportRole::Hole}};
}

namespace {
struct EdgeChain {
    std::vector<TerrainVec3> points;
    bool closed{};
    TerrainEdgeRef first; // smallest (body, edge) in the chain
};
// Chains one group of edges (sorted, distinct) into polylines; see terrainEdgeSources.
std::vector<EdgeChain> chainEdges(const Document &doc, std::vector<TerrainEdgeRef> edges,
                                  std::uint64_t &zeroLength) {
    std::sort(edges.begin(), edges.end());
    constexpr double tolerance = 1e-7, cell = 1e-6;
    std::vector<TerrainVec3> nodes;
    std::map<std::array<std::int64_t, 3>, std::vector<std::size_t>> grid;
    std::map<std::pair<Id, Id>, std::size_t> byVertex; // (body, vertex) -> node
    auto node = [&](Id body, Id vertex, const Vec3 &p) {
        if (const auto found = byVertex.find({body, vertex}); found != byVertex.end())
            return found->second;
        const std::array<std::int64_t, 3> key{std::int64_t(std::floor(p.x / cell)),
                                              std::int64_t(std::floor(p.y / cell)),
                                              std::int64_t(std::floor(p.z / cell))};
        std::optional<std::size_t> match;
        for (std::int64_t dx = -1; dx <= 1 && !match; ++dx)
            for (std::int64_t dy = -1; dy <= 1 && !match; ++dy)
                for (std::int64_t dz = -1; dz <= 1 && !match; ++dz) {
                    const auto found = grid.find({key[0] + dx, key[1] + dy, key[2] + dz});
                    if (found == grid.end())
                        continue;
                    for (const auto index : found->second) {
                        const auto &q = nodes[index];
                        if (std::abs(q.x - p.x) <= tolerance && std::abs(q.y - p.y) <= tolerance &&
                            std::abs(q.z - p.z) <= tolerance) {
                            match = index;
                            break;
                        }
                    }
                }
        const auto index = match.value_or(nodes.size());
        if (!match) {
            nodes.push_back({p.x, p.y, p.z});
            grid[key].push_back(index);
        }
        byVertex[{body, vertex}] = index;
        return index;
    };
    struct Link {
        std::size_t a, b;
        TerrainEdgeRef ref;
    };
    std::vector<Link> links;
    for (const auto &ref : edges) {
        const auto &body = *doc.bodies().at(ref.body);
        const auto &edge = body.topology.edges.at(ref.edge);
        const auto world = doc.worldTransform(ref.body);
        const auto a = node(ref.body, edge.a, world.point(body.surface.vertices.at(edge.a)));
        const auto b = node(ref.body, edge.b, world.point(body.surface.vertices.at(edge.b)));
        if (a == b) {
            ++zeroLength;
            continue;
        }
        links.push_back({a, b, ref});
    }
    std::vector<std::vector<std::size_t>> incident(nodes.size());
    for (std::size_t i = 0; i < links.size(); ++i) {
        incident[links[i].a].push_back(i);
        incident[links[i].b].push_back(i);
    }
    std::vector<bool> used(links.size());
    std::vector<EdgeChain> chains;
    auto walk = [&](std::size_t start, std::size_t link) {
        EdgeChain chain;
        chain.first = links[link].ref;
        chain.points.push_back(nodes[start]);
        auto at = start;
        while (true) {
            used[link] = true;
            chain.first = std::min(chain.first, links[link].ref);
            const auto next = links[link].a == at ? links[link].b : links[link].a;
            if (next == start) {
                chain.closed = true;
                break;
            }
            chain.points.push_back(nodes[next]);
            at = next;
            if (incident[at].size() != 2)
                break;
            const auto other = incident[at][0] == link ? incident[at][1] : incident[at][0];
            if (used[other])
                break;
            link = other;
        }
        chains.push_back(std::move(chain));
    };
    // Open polylines from every end or branch node, in node order, then the closed loops.
    for (std::size_t n = 0; n < nodes.size(); ++n)
        if (incident[n].size() != 2)
            for (const auto link : incident[n])
                if (!used[link])
                    walk(n, link);
    for (std::size_t i = 0; i < links.size(); ++i)
        if (!used[i])
            walk(links[i].a, i);
    return chains;
}
} // namespace

TerrainSourceSet terrainEdgeSources(const Document &doc, const TerrainEdgeSelection &selection) {
    std::set<TerrainEdgeRef> seen;
    auto checkEdge = [&](const TerrainEdgeRef &ref) {
        const auto name = "body " + std::to_string(ref.body) + " edge " + std::to_string(ref.edge);
        const auto found = doc.bodies().find(ref.body);
        if (found == doc.bodies().end())
            throw TerrainError("NOT_FOUND", "Body " + std::to_string(ref.body) + " does not exist");
        const auto &body = *found->second;
        if (body.terrainTile || body.kind == BodyKind::Terrain)
            throw TerrainError("UNSUPPORTED_TARGET", terrainProtectedMessage);
        if (!body.topology.edges.contains(ref.edge))
            throw TerrainError("NOT_FOUND", "Edge " + name + " does not exist");
        for (Id id = ref.body; id && doc.bodies().contains(id); id = doc.bodies().at(id)->parent)
            if (doc.bodies().at(id)->locked)
                throw TerrainError("INVALID_OPERATION",
                                   "Cannot use edges of a locked entity (" + name + ")");
        if (!seen.insert(ref).second)
            throw TerrainError("INVALID_REQUEST", "Edge " + name + " is given more than once");
    };
    std::vector<std::pair<QString, const std::vector<TerrainEdgeRef> *>> groups{
        {"edges", &selection.edges},
        {"contour", &selection.contours},
        {"breakline", &selection.breaklines},
        {"boundary", &selection.boundary}};
    for (const auto &hole : selection.holes)
        groups.push_back({"hole", &hole});
    std::size_t total = 0;
    for (const auto &[layer, edges] : groups) {
        for (const auto &ref : *edges)
            checkEdge(ref);
        total += edges->size();
    }
    if (!total)
        throw TerrainError("INVALID_REQUEST", "Choose at least one edge");
    TerrainSourceSet set;
    set.format = "edges";
    set.unit = *terrainUnit("m");
    for (const char *layer : {"edges", "contour", "breakline", "boundary", "hole"})
        set.layers.push_back({layer, 0, 0, 0, 0, 0});
    auto layerIndex = [](const QString &name) {
        return name == "edges"       ? 0u
               : name == "contour"   ? 1u
               : name == "breakline" ? 2u
               : name == "boundary"  ? 3u
                                     : 4u;
    };
    std::uint64_t zeroLength = 0, vertices = 0;
    for (const auto &[layer, edges] : groups) {
        if (edges->empty())
            continue;
        for (auto &chain : chainEdges(doc, *edges, zeroLength)) {
            TerrainSourcePolyline polyline;
            polyline.ref = {TerrainSourceRef::Kind::Edge, chain.first.edge, chain.first.body};
            polyline.layer = layerIndex(layer);
            polyline.points = std::move(chain.points);
            polyline.closed = chain.closed;
            vertices += polyline.points.size();
            if (vertices > TerrainSourceLimits::vertices)
                throw TerrainError("LIMIT_EXCEEDED", "Terrain edges exceed 1,000,000 vertices");
            auto &counts = set.layers[polyline.layer];
            ++counts.polylines;
            counts.closed += polyline.closed;
            const bool constant =
                std::all_of(polyline.points.begin(), polyline.points.end(),
                            [&](const TerrainVec3 &p) { return p.z == polyline.points.front().z; });
            ++(constant ? counts.constantZ : counts.varyingZ);
            set.polylines.push_back(std::move(polyline));
        }
    }
    if (zeroLength)
        set.problems.add(TerrainReport::Severity::Notice, "zero-length-edges", zeroLength,
                         "Edges whose ends meet within 1e-7 m were dropped.");
    set.report = {{"apiVersion", 1},
                  {"format", "edges"},
                  {"units", "m"},
                  {"metresPerUnit", 1},
                  {"metresPerUnitExact", "1"},
                  {"edges", double(total)},
                  {"polylines", double(set.polylines.size())},
                  {"sha256", ""}};
    return set;
}


// ---- R095.h: roles by tag ------------------------------------------------------------------
Id terrainEdgeTag(const Document &doc, Id body) {
    for (Id id = body; id; ) {
        const auto found = doc.bodies().find(id);
        if (found == doc.bodies().end())
            return 0;
        if (found->second->tag)
            return found->second->tag;
        id = found->second->parent;
    }
    return 0;
}
std::string terrainTagName(const Document &doc, Id tag) {
    if (!tag)
        return "Untagged";
    const auto found = doc.tags().find(tag);
    return found == doc.tags().end() ? "Tag " + std::to_string(tag) : found->second->name;
}
std::vector<TerrainTagEdges> terrainEdgesByTag(const Document &doc,
                                               const std::vector<TerrainEdgeRef> &edges) {
    std::map<Id, std::vector<TerrainEdgeRef>> byTag;
    for (const auto &ref : edges)
        byTag[terrainEdgeTag(doc, ref.body)].push_back(ref);
    std::vector<TerrainTagEdges> tags;
    for (auto &[tag, list] : byTag) {
        std::sort(list.begin(), list.end());
        TerrainEdgeSelection one;
        one.edges = list;
        const auto set = terrainEdgeSources(doc, one);
        const auto &layer = set.layers.front(); // "edges"
        tags.push_back({tag, std::move(list), layer.polylines, layer.closed, layer.constantZ,
                        layer.varyingZ});
    }
    return tags;
}
bool terrainEdgesHaveLoop(const Document &doc, const std::vector<TerrainEdgeRef> &edges) {
    TerrainEdgeSelection all;
    all.edges = edges;
    return terrainEdgeSources(doc, all).layers.front().closed > 0;
}
bool terrainSelectionNeedsRoles(const Document &doc, const std::vector<TerrainEdgeRef> &edges) {
    if (edges.empty())
        return false;
    const auto tag = terrainEdgeTag(doc, edges.front().body);
    for (const auto &ref : edges)
        if (terrainEdgeTag(doc, ref.body) != tag)
            return true;
    return terrainEdgesHaveLoop(doc, edges);
}
TerrainEdgeSelection terrainTagRoleSelection(const std::vector<TerrainTagEdges> &tags,
                                             const std::map<Id, TerrainImportRole> &roles) {
    TerrainEdgeSelection selection;
    auto append = [](std::vector<TerrainEdgeRef> &into, const std::vector<TerrainEdgeRef> &from) {
        into.insert(into.end(), from.begin(), from.end());
    };
    for (const auto &tag : tags) {
        const auto found = roles.find(tag.tag);
        switch (found == roles.end() ? TerrainImportRole::Auto : found->second) {
        case TerrainImportRole::Auto:
            append(selection.edges, tag.edges);
            break;
        case TerrainImportRole::Contour:
            append(selection.contours, tag.edges);
            break;
        case TerrainImportRole::Breakline:
            append(selection.breaklines, tag.edges);
            break;
        case TerrainImportRole::Boundary:
            append(selection.boundary, tag.edges);
            break;
        case TerrainImportRole::Hole:
            selection.holes.push_back(tag.edges);
            break;
        case TerrainImportRole::Ignore:
            break;
        case TerrainImportRole::Ground:
            throw std::invalid_argument("Ground points is not a role for model edges");
        }
    }
    return selection;
}
std::vector<std::string> terrainTagRoleProblems(const Document &doc,
                                                const std::vector<TerrainTagEdges> &tags,
                                                const std::map<Id, TerrainImportRole> &roles) {
    auto roleOf = [&](Id tag) {
        const auto found = roles.find(tag);
        return found == roles.end() ? TerrainImportRole::Auto : found->second;
    };
    auto quoted = [&](Id tag) { return "“" + terrainTagName(doc, tag) + "”"; };
    auto counted = [](std::uint64_t n, const char *one, const char *many) {
        return std::to_string(n) + ' ' + (n == 1 ? one : many);
    };
    std::vector<std::string> problems;
    std::vector<Id> boundaries;
    bool used = false;
    for (const auto &tag : tags) {
        const auto role = roleOf(tag.tag);
        used |= role != TerrainImportRole::Ignore;
        if (role == TerrainImportRole::Boundary) {
            boundaries.push_back(tag.tag);
            if (tag.open())
                problems.push_back("Boundary " + quoted(tag.tag) + " has " +
                                   counted(tag.open(), "open chain", "open chains") +
                                   "; a boundary is one closed loop.");
            else if (tag.closed != 1)
                problems.push_back("Boundary " + quoted(tag.tag) + " has " +
                                   counted(tag.closed, "closed loop", "closed loops") +
                                   "; a boundary is one closed loop.");
        } else if (role == TerrainImportRole::Hole && tag.open())
            problems.push_back("Hole " + quoted(tag.tag) + " has " +
                               counted(tag.open(), "open chain", "open chains") +
                               "; holes are closed loops.");
    }
    if (boundaries.size() > 1) {
        std::string names;
        for (std::size_t i = 0; i < boundaries.size(); ++i)
            names += (i ? (i + 1 == boundaries.size() ? " and " : ", ") : "") + quoted(boundaries[i]);
        problems.insert(problems.begin(),
                        "Only one tag can be the boundary; " + names +
                            (boundaries.size() == 2 ? " both" : " all") + " have that role.");
    }
    if (!used)
        problems.push_back("Every tag is ignored; give at least one tag a role.");
    return problems;
}

} // namespace sketchy
