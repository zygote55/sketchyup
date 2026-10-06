#include "geometry/section.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <map>
#include <set>
namespace sketchy {
void SectionPlane::validate() const {
    for (auto v : {normal.x, normal.y, normal.z, offset})
        if (!std::isfinite(v) || std::abs(v) > 2 * coordinateLimit)
            throw std::runtime_error("Section plane must have bounded finite coefficients");
    if (std::abs(dot(normal, normal) - 1) > 1e-8)
        throw std::runtime_error("Section plane requires a unit normal");
}
SectionPlane SectionPlane::through(Vec3 point, Vec3 direction) {
    checkPoint(point);
    const auto n = normalized(direction);
    SectionPlane result{n, -dot(n, point)};
    result.validate();
    return result;
}
SectionPlane SectionPlane::transformed(const Transform &frame) const {
    validate();
    const auto inverse = frame.inverse();
    // Plane covectors use the inverse transpose, including reflection and shear.
    const Vec3 n{inverse.m[0] * normal.x + inverse.m[1] * normal.y + inverse.m[2] * normal.z,
                 inverse.m[4] * normal.x + inverse.m[5] * normal.y + inverse.m[6] * normal.z,
                 inverse.m[8] * normal.x + inverse.m[9] * normal.y + inverse.m[10] * normal.z};
    const auto magnitude = length(n);
    if (!std::isfinite(magnitude) || magnitude == 0)
        throw std::runtime_error("Invalid transformed section normal");
    SectionPlane result{n * (1 / magnitude),
                        (offset + dot(normal, {inverse.m[12], inverse.m[13], inverse.m[14]})) /
                            magnitude};
    result.validate();
    return result;
}
namespace {
void validateCuts(const std::vector<SectionCut> &cuts) {
    if (cuts.size() > sectionPlaneLimit)
        throw std::runtime_error("Section view exceeds eight active planes");
    std::set<Id> ids;
    for (const auto &cut : cuts) {
        if (!cut.id || !ids.insert(cut.id).second)
            throw std::runtime_error("Section plane IDs must be nonzero and unique");
        cut.plane.validate();
    }
}
using Point = std::pair<int64_t, int64_t>;
using ContourEdge = std::pair<Point, Point>;
struct PlaneFrame {
    Vec3 origin, u, v;
    explicit PlaneFrame(const SectionPlane &plane) {
        origin = plane.normal * -plane.offset;
        u = normalized(
            cross(std::abs(plane.normal.z) < .9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, plane.normal));
        v = cross(plane.normal, u);
    }
    Point project(Vec3 p) const {
        const auto d = p - origin;
        return {std::llround(dot(d, u) / tolerance), std::llround(dot(d, v) / tolerance)};
    }
};
struct EdgeCount {
    unsigned count{};
    int balance{};
    bool coplanar{};
};
SectionVertex interpolate(const SectionVertex &a, const SectionVertex &b, double t) {
    SectionVertex result{a.point + (b.point - a.point) * t};
    for (size_t i = 0; i < 3; ++i)
        result.weights[i] = a.weights[i] * (1 - t) + b.weights[i] * t;
    return result;
}
double snappedDistance(const SectionPlane &plane, Vec3 p) {
    const auto d = plane.distance(p);
    return std::abs(d) <= tolerance ? 0 : d;
}
void appendTriangle(SectionMesh &mesh, SectionTriangle triangle) {
    const auto &v = triangle.vertices;
    if (length(cross(v[1].point - v[0].point, v[2].point - v[0].point)) <= tolerance * tolerance)
        return;
    if (mesh.triangles.size() == sectionOutputTriangleLimit)
        throw std::runtime_error("Section output exceeds triangle budget");
    mesh.triangles.push_back(std::move(triangle));
}
void markUnfilled(SectionMesh &mesh, Id id) {
    if (std::find(mesh.unfilledSections.begin(), mesh.unfilledSections.end(), id) ==
        mesh.unfilledSections.end())
        mesh.unfilledSections.push_back(id);
}
void cap(SectionMesh &mesh, const SectionCut &cut, const std::map<ContourEdge, EdgeCount> &counts,
         const std::map<Point, Vec3> &exact, size_t &work) {
    using namespace Clipper2Lib;
    std::map<Point, std::set<Point>> adjacent;
    bool ambiguous{};
    for (const auto &[edge, count] : counts) {
        ambiguous |= count.count > 2 || (count.count == 2 && count.balance != 0);
        if ((count.count == 2 && count.balance == 0) || count.coplanar)
            continue;
        const auto &[a, b] = edge;
        adjacent[a].insert(b);
        adjacent[b].insert(a);
        mesh.edges.push_back({exact.at(a), exact.at(b), cut.id});
    }
    if (ambiguous) {
        markUnfilled(mesh, cut.id);
        return;
    }
    if (adjacent.empty())
        return;
    if (adjacent.size() > 65536)
        throw std::runtime_error("Section contour exceeds vertex budget");
    // A branch or open endpoint is not an implicit face. Keep cut edges visible.
    for (const auto &[point, neighbors] : adjacent)
        if (neighbors.size() != 2) {
            markUnfilled(mesh, cut.id);
            return;
        }
    Paths64 loops;
    std::set<Point> visited;
    for (const auto &[start, unused] : adjacent) {
        if (visited.contains(start))
            continue;
        Path64 path;
        Point current = start, previous = start;
        do {
            if (!visited.insert(current).second)
                throw std::runtime_error("Ambiguous section contour traversal");
            path.emplace_back(current.first, current.second);
            const auto &neighbors = adjacent.at(current);
            auto next = *neighbors.begin();
            if (next == previous)
                next = *neighbors.rbegin();
            previous = current;
            current = next;
        } while (current != start);
        if (path.size() < 3 || std::abs(Area(path)) < 1) {
            markUnfilled(mesh, cut.id);
            return;
        }
        loops.push_back(std::move(path));
    }
    double expectedArea{};
    for (size_t i = 0; i < loops.size(); ++i) {
        unsigned depth{};
        for (size_t j = 0; j < loops.size(); ++j) {
            if (i == j)
                continue;
            if (work < loops[j].size())
                throw std::runtime_error("Section contour nesting exceeds work budget");
            work -= loops[j].size();
            const auto location = PointInPolygon(loops[i][0], loops[j]);
            if (location == PointInPolygonResult::IsOn) {
                markUnfilled(mesh, cut.id);
                return;
            }
            depth += location == PointInPolygonResult::IsInside;
        }
        if (IsPositive(loops[i]) != (depth % 2 == 0))
            std::reverse(loops[i].begin(), loops[i].end());
        const auto origin = exact.at({loops[i][0].x, loops[i][0].y});
        Vec3 areaVector{};
        for (size_t k = 0; k < loops[i].size(); ++k) {
            const auto a = loops[i][k], b = loops[i][(k + 1) % loops[i].size()];
            areaVector =
                areaVector + cross(exact.at({a.x, a.y}) - origin, exact.at({b.x, b.y}) - origin);
        }
        expectedArea += dot(areaVector, cut.plane.normal) * .5;
    }
    Paths64 triangles;
    if (Triangulate(loops, triangles) != TriangulateResult::success) {
        markUnfilled(mesh, cut.id);
        return;
    }
    double area{};
    std::vector<SectionTriangle> caps;
    for (const auto &triangle : triangles) {
        if (triangle.size() != 3)
            throw std::runtime_error("Section cap triangulation returned a non-triangle");
        SectionTriangle output{};
        output.section = cut.id;
        for (size_t i = 0; i < 3; ++i)
            output.vertices[i].point = exact.at({triangle[i].x, triangle[i].y});
        const auto n = cross(output.vertices[1].point - output.vertices[0].point,
                             output.vertices[2].point - output.vertices[0].point);
        area += length(n) * .5;
        if (dot(n, cut.plane.normal) > 0)
            std::swap(output.vertices[1], output.vertices[2]);
        caps.push_back(output);
    }
    if (std::abs(area - expectedArea) > std::max(1e-8, std::abs(expectedArea) * 1e-8))
        throw std::runtime_error("Section cap area does not match its contours");
    for (auto &triangle : caps)
        appendTriangle(mesh, std::move(triangle));
}
std::optional<std::array<Vec3, 2>> clipSegment(Vec3 a, Vec3 b, const SectionPlane &plane) {
    const auto da = snappedDistance(plane, a), db = snappedDistance(plane, b);
    if (da < 0 && db < 0)
        return {};
    if (da < 0)
        a = a + (b - a) * (da / (da - db));
    else if (db < 0)
        b = a + (b - a) * (da / (da - db));
    if (length(b - a) <= tolerance)
        return {};
    return std::array{a, b};
}
} // namespace
bool sectionContains(Vec3 point, const std::vector<SectionCut> &cuts) {
    checkPoint(point);
    validateCuts(cuts);
    return std::all_of(cuts.begin(), cuts.end(),
                       [&](const auto &cut) { return cut.plane.distance(point) >= -tolerance; });
}
std::optional<std::array<Vec3, 2>> sectionSegment(Vec3 a, Vec3 b,
                                                  const std::vector<SectionCut> &cuts) {
    checkPoint(a);
    checkPoint(b);
    validateCuts(cuts);
    for (const auto &cut : cuts) {
        auto clipped = clipSegment(a, b, cut.plane);
        if (!clipped)
            return {};
        a = (*clipped)[0];
        b = (*clipped)[1];
    }
    if (length(b - a) <= tolerance)
        return {};
    return std::array{a, b};
}
SectionMesh sectionMesh(const std::vector<Triangle> &source, const std::vector<SectionCut> &cuts) {
    validateCuts(cuts);
    if (source.size() > sectionInputTriangleLimit)
        throw std::runtime_error("Section source exceeds triangle budget");
    SectionMesh mesh;
    for (size_t i = 0; i < source.size(); ++i) {
        const auto &t = source[i];
        for (auto p : {t.a, t.b, t.c})
            checkPoint(p);
        appendTriangle(mesh,
                       {{{{t.a, {1, 0, 0}}, {t.b, {0, 1, 0}}, {t.c, {0, 0, 1}}}}, t.face, 0, i});
    }
    size_t work = 4000000;
    for (const auto &cut : cuts) {
        SectionMesh next;
        next.unfilledSections = mesh.unfilledSections;
        for (const auto &edge : mesh.edges)
            if (auto clipped = clipSegment(edge.a, edge.b, cut.plane))
                next.edges.push_back({(*clipped)[0], (*clipped)[1], edge.section});
        PlaneFrame frame(cut.plane);
        std::map<ContourEdge, EdgeCount> boundary;
        std::map<Point, Vec3> exact;
        bool removed{};
        for (const auto &triangle : mesh.triangles) {
            if (!work--)
                throw std::runtime_error("Section clipping exceeds work budget");
            std::vector<SectionVertex> polygon;
            for (size_t i = 0; i < 3; ++i) {
                auto a = triangle.vertices[i], b = triangle.vertices[(i + 1) % 3];
                const auto da = snappedDistance(cut.plane, a.point),
                           db = snappedDistance(cut.plane, b.point);
                removed |= da < 0;
                if (da >= 0)
                    polygon.push_back(a);
                if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
                    auto intersection = interpolate(a, b, da / (da - db));
                    intersection.point = intersection.point -
                                         cut.plane.normal * cut.plane.distance(intersection.point);
                    polygon.push_back(intersection);
                }
            }
            if (polygon.size() < 3)
                continue;
            const bool coplanar = std::all_of(polygon.begin(), polygon.end(), [&](const auto &v) {
                return snappedDistance(cut.plane, v.point) == 0;
            });
            for (size_t i = 0; i < polygon.size(); ++i) {
                const auto &a = polygon[i].point, &b = polygon[(i + 1) % polygon.size()].point;
                if (snappedDistance(cut.plane, a) != 0 || snappedDistance(cut.plane, b) != 0)
                    continue;
                auto pa = frame.project(a), pb = frame.project(b);
                if (pa == pb)
                    continue;
                exact.try_emplace(pa, a);
                exact.try_emplace(pb, b);
                const int direction = pb < pa ? -1 : 1;
                if (pb < pa)
                    std::swap(pa, pb);
                auto &count = boundary[{pa, pb}];
                ++count.count;
                count.balance += direction;
                count.coplanar |= coplanar;
            }
            for (size_t i = 1; i + 1 < polygon.size(); ++i)
                appendTriangle(next, {{{polygon[0], polygon[i], polygon[i + 1]}},
                                      triangle.face,
                                      triangle.section,
                                      triangle.source});
        }
        if (removed)
            cap(next, cut, boundary, exact, work);
        mesh = std::move(next);
    }
    return mesh;
}
} // namespace sketchy
