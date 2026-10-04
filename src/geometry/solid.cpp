#include "geometry/solid.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <set>
namespace sketchy {
namespace {
using Pair = std::pair<Id, Id>;
struct Boundary {
    std::vector<Vec3> vertices;
    std::vector<std::array<Vec3, 2>> edges;
};
bool contains(const Boundary &boundary, Vec3 point) {
    for (auto vertex : boundary.vertices)
        if (length(point - vertex) <= tolerance * 4)
            return true;
    for (auto edge : boundary.edges) {
        const auto delta = edge[1] - edge[0];
        const auto size = length(delta);
        const auto along = dot(point - edge[0], delta) / size;
        if (along >= -tolerance * 4 && along <= size + tolerance * 4 &&
            length(cross(point - edge[0], delta)) / size <= tolerance * 4)
            return true;
    }
    return false;
}
bool contains(const Boundary &boundary, Vec3 a, Vec3 b) {
    const auto size = length(b - a);
    if (size <= tolerance * 4)
        return contains(boundary, (a + b) * .5);
    const auto direction = (b - a) * (1 / size);
    std::vector<std::pair<double, double>> spans;
    for (auto edge : boundary.edges)
        if (length(cross(edge[0] - a, direction)) <= tolerance * 4 &&
            length(cross(edge[1] - a, direction)) <= tolerance * 4) {
            const auto u = dot(edge[0] - a, direction), v = dot(edge[1] - a, direction);
            spans.emplace_back(std::min(u, v), std::max(u, v));
        }
    std::sort(spans.begin(), spans.end());
    double reached = 0;
    for (auto [low, high] : spans) {
        if (low > reached + tolerance * 4)
            break;
        reached = std::max(reached, high);
    }
    return reached >= size - tolerance * 4;
}
std::array<Vec3, 3> points(const Triangle &t) { return {t.a, t.b, t.c}; }
bool inside(Vec3 point, const Triangle &t, Vec3 normal) {
    const auto p = points(t);
    for (size_t i = 0; i < 3; ++i) {
        const auto edge = p[(i + 1) % 3] - p[i];
        if (dot(normal, cross(edge, point - p[i])) < -tolerance * length(edge))
            return false;
    }
    return true;
}
void contacts(const Triangle &source, const Triangle &target, Vec3 normal,
              std::vector<Vec3> &result) {
    const auto p = points(source);
    for (size_t i = 0; i < 3; ++i) {
        const auto a = p[i], b = p[(i + 1) % 3];
        const auto da = dot(a - target.a, normal), db = dot(b - target.a, normal);
        if (std::abs(da) <= tolerance && inside(a, target, normal))
            result.push_back(a);
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
            const auto point = a + (b - a) * (da / (da - db));
            if (inside(point, target, normal))
                result.push_back(point);
        }
    }
}
bool coplanarConflict(const Triangle &a, const Triangle &b, Vec3 normal, const Boundary &boundary) {
    using namespace Clipper2Lib;
    const auto u = normalized(a.b - a.a), v = cross(normal, u);
    auto path = [&](const Triangle &triangle) {
        Path64 result;
        for (auto point : points(triangle)) {
            const auto relative = point - a.a;
            result.emplace_back(std::llround(dot(relative, u) / tolerance),
                                std::llround(dot(relative, v) / tolerance));
        }
        return result;
    };
    if (std::abs(Area(Intersect(Paths64{path(a)}, Paths64{path(b)}, FillRule::NonZero))) >= 1)
        return true;
    // Zero-area contacts still need shared topology. This also catches coincident
    // vertices/edges with distinct identities and folded coplanar surfaces.
    auto cross2 = [&](Vec3 x, Vec3 y) { return dot(normal, cross(x, y)); };
    const auto p = points(a), q = points(b);
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j) {
            const auto r = p[(i + 1) % 3] - p[i], s = q[(j + 1) % 3] - q[j];
            const auto offset = q[j] - p[i];
            const auto denominator = cross2(r, s);
            if (denominator != 0) {
                const auto t = cross2(offset, s) / denominator;
                const auto w = cross2(offset, r) / denominator;
                if (t >= -tolerance / length(r) && t <= 1 + tolerance / length(r) &&
                    w >= -tolerance / length(s) && w <= 1 + tolerance / length(s) &&
                    !contains(boundary, p[i] + r * std::clamp(t, 0., 1.)))
                    return true;
            } else if (length(cross(offset, r)) / length(r) <= tolerance) {
                const auto size = length(r);
                const auto direction = r * (1 / size);
                const auto start = dot(offset, direction),
                           end = dot(q[(j + 1) % 3] - p[i], direction);
                const auto low = std::max(0., std::min(start, end));
                const auto high = std::min(size, std::max(start, end));
                if (low <= high + tolerance && !contains(boundary, p[i] + direction * low,
                                                         p[i] + direction * std::max(low, high)))
                    return true;
            }
        }
    return false;
}
bool conflict(const Triangle &a, const Triangle &b, const Boundary &boundary) {
    const auto na = normalized(cross(a.b - a.a, a.c - a.a));
    const auto nb = normalized(cross(b.b - b.a, b.c - b.a));
    auto separated = [](const Triangle &t, Vec3 origin, Vec3 normal) {
        const auto p = points(t);
        bool positive = true, negative = true;
        for (auto point : p) {
            const auto distance = dot(point - origin, normal);
            positive &= distance > tolerance;
            negative &= distance < -tolerance;
        }
        return positive || negative;
    };
    if (separated(a, b.a, nb) || separated(b, a.a, na))
        return false;
    bool coplanar = length(cross(na, nb)) <= 1e-12;
    for (auto point : points(b))
        coplanar &= std::abs(dot(point - a.a, na)) <= tolerance;
    if (coplanar)
        return coplanarConflict(a, b, na, boundary);
    std::vector<Vec3> intersection;
    contacts(a, b, nb, intersection);
    contacts(b, a, na, intersection);
    if (intersection.empty())
        return false;
    // The intersection of two non-coplanar triangles is a point or segment.
    Vec3 first = intersection.front(), last = first;
    double widest = 0;
    for (auto x : intersection)
        for (auto y : intersection)
            if (length(y - x) > widest) {
                widest = length(y - x);
                first = x;
                last = y;
            }
    return !contains(boundary, first, last);
}
struct BoundedTriangle {
    Triangle triangle;
    Vec3 low, high;
};
BoundedTriangle bounded(Triangle t) {
    auto low = t.a, high = t.a;
    for (auto p : points(t)) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    return {t, low, high};
}
} // namespace
SolidReport inspectSolid(const Surface &surface, const Topology &topology) {
    if (surface.faces.empty())
        return {"empty", {}};
    if (!surface.wires.empty())
        return {"loose_geometry", {}};
    const auto adjacency = topology.adjacency(surface);
    std::map<Id, std::set<Id>> neighbors, incident;
    std::map<Pair, Boundary> boundaries;
    for (const auto &[edge, faces] : adjacency.edgeFaces) {
        if (faces.size() < 2)
            return {"open_boundary", {}, {}, {edge}};
        if (faces.size() != 2)
            return {"non_manifold", {}, {}, {edge}};
        if (faces[0].reversed == faces[1].reversed)
            return {"inconsistent_winding", {}, {faces[0].face, faces[1].face}, {edge}};
        const auto a = faces[0].face, b = faces[1].face;
        neighbors[a].insert(b);
        neighbors[b].insert(a);
        const auto &record = topology.edges.at(edge);
        boundaries[std::minmax(a, b)].edges.push_back(
            {surface.vertices.at(record.a), surface.vertices.at(record.b)});
        for (auto vertex : {record.a, record.b}) {
            incident[vertex].insert(a);
            incident[vertex].insert(b);
        }
    }
    size_t boundaryBudget = 200000;
    for (const auto &[vertex, point] : surface.vertices) {
        if (!incident.contains(vertex))
            return {"loose_geometry", {}, {}, {}, {vertex}};
        const auto &faces = incident.at(vertex);
        const auto pairs = faces.size() * (faces.size() - 1) / 2;
        if (pairs > boundaryBudget)
            return {"analysis_limit", {}};
        boundaryBudget -= pairs;
        std::map<Id, std::set<Id>> fan;
        for (auto edge : adjacency.vertexEdges.at(vertex)) {
            const auto &pair = adjacency.edgeFaces.at(edge);
            fan[pair[0].face].insert(pair[1].face);
            fan[pair[1].face].insert(pair[0].face);
        }
        std::set<Id> visited;
        std::vector<Id> queue{*faces.begin()};
        while (!queue.empty()) {
            const auto face = queue.back();
            queue.pop_back();
            if (!visited.insert(face).second)
                continue;
            for (auto next : fan[face])
                queue.push_back(next);
        }
        if (visited != faces)
            return {"non_manifold", {}, {}, {}, {vertex}};
        for (auto a : faces)
            for (auto b : faces)
                if (a < b)
                    boundaries[{a, b}].vertices.push_back(point);
    }
    std::set<Id> connected;
    std::vector<Id> queue{surface.faces.begin()->first};
    while (!queue.empty()) {
        const auto face = queue.back();
        queue.pop_back();
        if (!connected.insert(face).second)
            continue;
        for (auto next : neighbors[face])
            queue.push_back(next);
    }
    if (connected.size() != surface.faces.size())
        return {"multiple_shells", {}};
    std::vector<BoundedTriangle> triangles;
    for (const auto &[face, record] : surface.faces) {
        for (auto triangle : surface.triangulate(face))
            triangles.push_back(bounded(triangle));
        if (triangles.size() > 200000)
            return {"analysis_limit", {}};
    }
    std::sort(triangles.begin(), triangles.end(),
              [](const auto &a, const auto &b) { return a.low.x < b.low.x; });
    size_t budget = 1000000;
    const Boundary empty;
    for (size_t i = 0; i < triangles.size(); ++i) {
        const auto &a = triangles[i];
        for (size_t j = i + 1; j < triangles.size() && triangles[j].low.x <= a.high.x + tolerance;
             ++j) {
            if (!budget--)
                return {"analysis_limit", {}};
            const auto &b = triangles[j];
            if (a.triangle.face == b.triangle.face || a.low.y > b.high.y + tolerance ||
                b.low.y > a.high.y + tolerance || a.low.z > b.high.z + tolerance ||
                b.low.z > a.high.z + tolerance)
                continue;
            const Pair key = std::minmax(a.triangle.face, b.triangle.face);
            if (conflict(a.triangle, b.triangle,
                         boundaries.contains(key) ? boundaries.at(key) : empty))
                return {"self_intersection", {}, {key.first, key.second}};
        }
    }
    long double sum = 0;
    double area = 0;
    const auto origin = surface.vertices.begin()->second;
    for (const auto &entry : triangles) {
        const auto &t = entry.triangle;
        sum += dot(t.a - origin, cross(t.b - origin, t.c - origin));
        area += length(cross(t.b - t.a, t.c - t.a)) * .5;
    }
    const auto volume = double(std::abs(sum) / 6);
    if (!std::isfinite(volume) || volume <= tolerance * area / 3)
        return {"degenerate", {}};
    return {"solid", volume};
}
} // namespace sketchy
