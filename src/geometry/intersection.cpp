#include "geometry/intersection.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <limits>
#include <tuple>
namespace sketchy {
namespace {
constexpr size_t vertexLimit = 1024, edgeLimit = 4096;
constexpr double scale = 1e8;
[[noreturn]] void fail(const char *code, const char *message) {
    throw IntersectionError(code, message);
}
struct Region {
    std::vector<std::vector<Vec3>> loops;
    Vec3 normal, origin, low, high;
};
Region region(const Surface &surface, Id face) {
    const auto found = surface.faces.find(face);
    if (!face || found == surface.faces.end() || found->second.id != face)
        fail("INTERSECTION_INVALID_FACE", "Intersection requires existing planar faces");
    const auto &loops = found->second.loops;
    if (loops.empty() || loops.size() > 64)
        fail("INTERSECTION_LIMIT", "Intersection supports 1–64 boundary loops per face");
    size_t count{};
    Region result;
    for (const auto &loop : loops) {
        if (loop.size() < 3)
            fail("INTERSECTION_INVALID_FACE", "Face boundaries need at least three vertices");
        count += loop.size();
        if (count > vertexLimit)
            fail("INTERSECTION_LIMIT", "Intersection supports at most 1024 vertices per face");
        result.loops.emplace_back();
        for (auto vertex : loop) {
            if (!vertex || !surface.vertices.contains(vertex))
                fail("INTERSECTION_INVALID_FACE", "Face boundary has an invalid vertex");
            const auto point = surface.vertices.at(vertex);
            checkPoint(point);
            result.loops.back().push_back(point);
        }
    }
    (void)surface.triangulate(face);
    result.normal = surface.normal(face);
    result.low = result.high = result.loops[0][0];
    for (const auto &loop : result.loops)
        for (auto p : loop) {
            result.low = {std::min(result.low.x, p.x), std::min(result.low.y, p.y),
                          std::min(result.low.z, p.z)};
            result.high = {std::max(result.high.x, p.x), std::max(result.high.y, p.y),
                           std::max(result.high.z, p.z)};
        }
    const auto midpoint = (result.low + result.high) * .5;
    result.origin = midpoint - result.normal * dot(midpoint - result.loops[0][0], result.normal);
    return result;
}
bool apart(const Region &a, const Region &b) {
    return a.low.x > b.high.x + tolerance || b.low.x > a.high.x + tolerance ||
           a.low.y > b.high.y + tolerance || b.low.y > a.high.y + tolerance ||
           a.low.z > b.high.z + tolerance || b.low.z > a.high.z + tolerance;
}
std::pair<double, double> planeRange(const Region &a, const Region &plane) {
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (const auto &loop : a.loops)
        for (auto p : loop) {
            const auto d = dot(p - plane.origin, plane.normal);
            low = std::min(low, d);
            high = std::max(high, d);
        }
    return {low, high};
}
bool less(Vec3 a, Vec3 b) { return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z); }
void normalize(FaceIntersection &result) {
    std::sort(result.edges.begin(), result.edges.end(), [](auto x, auto y) {
        return less(x[0], y[0]) || (x[0] == y[0] && less(x[1], y[1]));
    });
    std::vector<std::array<Vec3, 2>> edges;
    size_t budget = 4000000;
    for (auto edge : result.edges) {
        for (size_t i = 0; i < edges.size();) {
            if (!budget--)
                fail("INTERSECTION_LIMIT", "Intersection edge normalization exceeds its budget");
            const auto start = edge[0], delta = edge[1] - start;
            const auto size = length(delta);
            const auto direction = delta * (1 / size);
            const auto x = edges[i][0] - start, y = edges[i][1] - start;
            const auto low = std::min(dot(x, direction), dot(y, direction));
            const auto high = std::max(dot(x, direction), dot(y, direction));
            if (length(cross(x, direction)) <= tolerance &&
                length(cross(y, direction)) <= tolerance && low <= size + tolerance &&
                high >= -tolerance) {
                edge = {start + direction * std::min(0., low),
                        start + direction * std::max(size, high)};
                edges.erase(edges.begin() + i);
                i = 0; // An extension may bridge a previously disjoint interval.
            } else {
                ++i;
            }
        }
        edges.push_back(edge);
    }
    std::sort(edges.begin(), edges.end(), [](auto x, auto y) {
        return less(x[0], y[0]) || (x[0] == y[0] && less(x[1], y[1]));
    });
    result.edges = std::move(edges);
}
void append(FaceIntersection &result, Vec3 a, Vec3 b) {
    checkPoint(a);
    checkPoint(b);
    if (length(b - a) <= tolerance)
        return;
    if (less(b, a))
        std::swap(a, b);
    result.edges.push_back({a, b});
    if (result.edges.size() > edgeLimit)
        fail("INTERSECTION_LIMIT", "Intersection exceeds 4096 output edges");
}
struct Point {
    double x, y;
};
double cross2(Point a, Point b) { return a.x * b.y - a.y * b.x; }
Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
// 0 outside, 1 inside, 2 on the boundary, independent of loop orientation.
int location(const std::vector<Point> &loop, Point p) {
    bool inside = false;
    for (size_t i = 0; i < loop.size(); ++i) {
        const auto a = loop[i], b = loop[(i + 1) % loop.size()], e = b - a, q = p - a;
        const auto length = std::hypot(e.x, e.y);
        const auto along = (q.x * e.x + q.y * e.y) / length;
        if (std::abs(cross2(e, q)) / length <= tolerance && along >= -tolerance &&
            along <= length + tolerance)
            return 2;
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside ? 1 : 0;
}
using Interval = std::pair<double, double>;
std::vector<Interval> intervals(const Region &region, Vec3 origin, Vec3 direction) {
    const auto vertical = cross(region.normal, direction);
    std::vector<std::vector<Point>> loops;
    std::vector<double> cuts;
    for (const auto &loop : region.loops) {
        loops.emplace_back();
        for (auto p : loop) {
            const auto relative = p - origin;
            loops.back().push_back({dot(relative, direction), dot(relative, vertical)});
        }
        for (size_t i = 0; i < loop.size(); ++i) {
            auto a = loops.back()[i], b = loops.back()[(i + 1) % loop.size()];
            if (std::abs(a.y) <= tolerance)
                a.y = 0;
            if (std::abs(b.y) <= tolerance)
                b.y = 0;
            if (a.y == 0)
                cuts.push_back(a.x);
            if (b.y == 0)
                cuts.push_back(b.x);
            if ((a.y < 0 && b.y > 0) || (a.y > 0 && b.y < 0))
                cuts.push_back(a.x + (b.x - a.x) * (-a.y / (b.y - a.y)));
        }
    }
    std::sort(cuts.begin(), cuts.end());
    std::vector<Interval> result;
    for (size_t i = 1; i < cuts.size(); ++i) {
        const auto low = cuts[i - 1], high = cuts[i];
        if (high - low <= tolerance)
            continue;
        const Point midpoint{(low + high) * .5, 0};
        if (!location(loops[0], midpoint))
            continue;
        bool hole = false;
        for (size_t h = 1; h < loops.size(); ++h)
            hole |= location(loops[h], midpoint) == 1;
        if (hole)
            continue;
        if (!result.empty() && low - result.back().second <= tolerance)
            result.back().second = high;
        else
            result.emplace_back(low, high);
    }
    return result;
}
FaceIntersection coplanar(const Region &a, const Region &b) {
    using namespace Clipper2Lib;
    const auto axis = std::abs(a.normal.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const auto u = normalized(cross(axis, a.normal)), v = cross(a.normal, u);
    const auto origin = a.origin;
    auto paths = [&](const Region &region) {
        Paths64 result;
        for (const auto &loop : region.loops) {
            Path64 path;
            for (auto p : loop) {
                const auto relative = p - origin;
                path.emplace_back(std::llround(dot(relative, u) * scale),
                                  std::llround(dot(relative, v) * scale));
            }
            if (IsPositive(path) != result.empty())
                std::reverse(path.begin(), path.end());
            result.push_back(std::move(path));
        }
        return result;
    };
    FaceIntersection result{true, {}};
    auto point = [&](Point64 p) {
        return origin + u * (double(p.x) / scale) + v * (double(p.y) / scale);
    };
    for (auto loop : Intersect(paths(a), paths(b), FillRule::NonZero)) {
        loop = TrimCollinear(loop);
        for (size_t i = 0; i < loop.size(); ++i) {
            const auto start = point(loop[i]), end = point(loop[(i + 1) % loop.size()]);
            if (length(end - start) <= tolerance)
                fail("INTERSECTION_BELOW_TOLERANCE",
                     "A common region has a sub-tolerance boundary; separate or align its faces");
            append(result, start, end);
        }
    }
    // Closed polygon clipping omits zero-area edge contacts. Retain their
    // finite overlap so a partial shared boundary can acquire matching splits.
    for (const auto &la : a.loops)
        for (size_t i = 0; i < la.size(); ++i) {
            const auto start = la[i], delta = la[(i + 1) % la.size()] - start;
            const auto size = length(delta);
            const auto direction = delta * (1 / size);
            for (const auto &lb : b.loops)
                for (size_t j = 0; j < lb.size(); ++j) {
                    const auto x = lb[j] - start, y = lb[(j + 1) % lb.size()] - start;
                    if (length(cross(x, direction)) > tolerance ||
                        length(cross(y, direction)) > tolerance)
                        continue;
                    const auto low = std::max(0., std::min(dot(x, direction), dot(y, direction)));
                    const auto high =
                        std::min(size, std::max(dot(x, direction), dot(y, direction)));
                    if (high > low + tolerance)
                        append(result, start + direction * low, start + direction * high);
                }
        }
    normalize(result);
    return result;
}
FaceIntersection build(const Surface &sourceA, Id faceA, const Surface &sourceB, Id faceB) {
    const auto a = region(sourceA, faceA), b = region(sourceB, faceB);
    if (apart(a, b))
        return {};
    const auto [blow, bhigh] = planeRange(b, a);
    const auto [alow, ahigh] = planeRange(a, b);
    if (blow > tolerance || bhigh < -tolerance || alow > tolerance || ahigh < -tolerance)
        return {};
    const auto crossNormal = cross(a.normal, b.normal);
    const auto sine = length(crossNormal);
    if (std::max({std::abs(alow), std::abs(ahigh), std::abs(blow), std::abs(bhigh)}) <= tolerance) {
        if (sine < 1e-8)
            return coplanar(a, b);
        fail("INTERSECTION_UNSTABLE",
             "The face separation is below modeling tolerance; enlarge the geometry");
    }
    if (sine < 1e-8)
        fail("INTERSECTION_UNSTABLE", "Nearly parallel faces exceed stable intersection precision; "
                                      "separate or align their planes");
    const auto direction = crossNormal * (1 / sine);
    const auto height = dot(b.origin - a.origin, b.normal);
    const auto origin = a.origin + cross(crossNormal, a.normal) * (height / (sine * sine));
    const auto first = intervals(a, origin, direction), second = intervals(b, origin, direction);
    FaceIntersection result;
    for (auto [a0, a1] : first)
        for (auto [b0, b1] : second) {
            const auto low = std::max(a0, b0), high = std::min(a1, b1);
            if (high > low + tolerance)
                append(result, origin + direction * low, origin + direction * high);
        }
    return result;
}
} // namespace
FaceIntersection intersectFaces(const Surface &a, Id faceA, const Surface &b, Id faceB) {
    try {
        return build(a, faceA, b, faceB);
    } catch (const IntersectionError &) {
        throw;
    } catch (const std::exception &) {
        throw IntersectionError("INTERSECTION_INVALID_FACE",
                                "Intersection requires valid planar faces with finite, noncrossing "
                                "boundaries and contained holes");
    }
}
} // namespace sketchy
