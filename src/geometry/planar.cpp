#include "geometry/planar.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <set>
namespace sketchy {
namespace {
using Key = std::array<Id, 2>;
using namespace Clipper2Lib;
constexpr size_t segmentLimit = 1024, pieceLimit = 16384;
constexpr double scale = 1e7;
struct Point {
    double x{}, y{};
    Point operator+(Point b) const { return {x + b.x, y + b.y}; }
    Point operator-(Point b) const { return {x - b.x, y - b.y}; }
    Point operator*(double s) const { return {x * s, y * s}; }
};
double cross2(Point a, Point b) { return a.x * b.y - a.y * b.x; }
double dot2(Point a, Point b) { return a.x * b.x + a.y * b.y; }
double size(Point a) { return std::hypot(a.x, a.y); }
Key key(Id a, Id b) { return {std::min(a, b), std::max(a, b)}; }
struct Plane {
    Vec3 origin, n, u, v;
    Point project(Vec3 point) const {
        const auto p = point - origin;
        return {dot(p, u), dot(p, v)};
    }
    Vec3 world(Point p) const { return origin + u * p.x + v * p.y; }
    bool contains(Vec3 p) const { return std::abs(dot(p - origin, n)) <= tolerance; }
};
struct Segment {
    Point a, b;
    bool inserted{};
    struct Cut {
        double parameter;
        Point point;
    };
    std::vector<Cut> cuts;
    Segment(Point start, Point end, bool isNew)
        : a(start), b(end), inserted(isNew), cuts{{0, start}, {1, end}} {}
};
void cut(Segment &segment, Point p) {
    const auto delta = segment.b - segment.a;
    const auto t = dot2(p - segment.a, delta) / dot2(delta, delta);
    const auto margin = tolerance / size(delta);
    if (t >= -margin && t <= 1 + margin &&
        std::abs(cross2(p - segment.a, delta)) / size(delta) <= tolerance)
        segment.cuts.push_back({std::clamp(t, 0.0, 1.0), p});
}
void intersect(Segment &a, Segment &b) {
    const auto r = a.b - a.a, s = b.b - b.a, d = b.a - a.a;
    const auto denominator = cross2(r, s), lengths = size(r) * size(s);
    if (std::abs(denominator) <= lengths * 1e-12) {
        if (std::abs(cross2(d, r)) / size(r) <= tolerance) {
            cut(a, b.a);
            cut(a, b.b);
            cut(b, a.a);
            cut(b, a.b);
        }
        return;
    }
    const auto t = cross2(d, s) / denominator, q = cross2(d, r) / denominator;
    const auto ta = tolerance / size(r), tb = tolerance / size(s);
    if (t >= -ta && t <= 1 + ta && q >= -tb && q <= 1 + tb) {
        if (std::abs(denominator) < lengths * 1e-8)
            throw PlanarError(
                "UNSTABLE_INTERSECTION",
                "Nearly parallel intersecting edges exceed the stable arrangement policy");
        const auto intersection = a.a + r * std::clamp(t, 0.0, 1.0);
        a.cuts.push_back({std::clamp(t, 0.0, 1.0), intersection});
        b.cuts.push_back({std::clamp(q, 0.0, 1.0), intersection});
    }
}
Path64 path(const std::vector<Id> &loop, const Surface &surface, const Plane &plane) {
    Path64 result;
    for (auto id : loop) {
        const auto p = plane.project(surface.vertices.at(id));
        result.emplace_back(std::llround(p.x * scale), std::llround(p.y * scale));
    }
    return result;
}
Point64 integer(Point p) { return {std::llround(p.x * scale), std::llround(p.y * scale)}; }
std::vector<Id> canonical(std::vector<Id> loop) {
    auto rotate = [](auto &values) {
        std::rotate(values.begin(), std::min_element(values.begin(), values.end()), values.end());
    };
    rotate(loop);
    auto reversed = loop;
    std::reverse(reversed.begin(), reversed.end());
    rotate(reversed);
    return std::min(loop, reversed);
}
std::vector<std::vector<Id>> canonical(const Face &face) {
    std::vector<std::vector<Id>> result;
    for (auto loop : face.loops)
        result.push_back(canonical(std::move(loop)));
    if (result.size() > 1)
        std::sort(result.begin() + 1, result.end());
    return result;
}
PlanarResult build(const Surface &source, Vec3 origin, Vec3 normal,
                   const std::vector<std::array<Vec3, 2>> &inserted, bool heal) {
    source.validate();
    checkPoint(origin);
    checkPoint(normal);
    if (inserted.empty() || inserted.size() > segmentLimit)
        throw PlanarError("ARRANGEMENT_LIMIT", "Insert between 1 and 1024 finite edges");
    const auto n = normalized(normal);
    const auto axis = std::abs(n.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    const auto u = normalized(cross(axis, n));
    const Plane plane{origin, n, u, cross(n, u)};
    PlanarResult result{source, {}};
    std::vector<Segment> segments;
    std::map<Key, size_t> sourceSegments;
    std::map<Id, Paths64> sourceFaces;
    for (const auto &[id, face] : source.faces) {
        bool coplanar = true;
        for (const auto &loop : face.loops)
            for (auto vertex : loop)
                coplanar = coplanar && plane.contains(source.vertices.at(vertex));
        if (coplanar) {
            for (const auto &loop : face.loops)
                sourceFaces[id].push_back(path(loop, source, plane));
            result.faces[id] = {};
        }
    }
    for (const auto &edge : source.edges()) {
        const auto a = source.vertices.at(edge.a), b = source.vertices.at(edge.b);
        if (plane.contains(a) && plane.contains(b)) {
            sourceSegments[key(edge.a, edge.b)] = segments.size();
            segments.push_back({plane.project(a), plane.project(b), false});
        }
    }
    for (const auto &edge : inserted) {
        checkPoint(edge[0]);
        checkPoint(edge[1]);
        if (!plane.contains(edge[0]) || !plane.contains(edge[1]))
            throw PlanarError(
                "NON_PLANAR_INPUT",
                "Inserted edge endpoints must lie on the editing plane within tolerance");
        segments.push_back({plane.project(edge[0]), plane.project(edge[1]), true});
    }
    if (segments.size() > segmentLimit)
        throw PlanarError("ARRANGEMENT_LIMIT",
                          "This planar context exceeds the 1024 segment arrangement budget");
    for (const auto &segment : segments)
        if (size(segment.b - segment.a) < tolerance)
            throw PlanarError("DEGENERATE_EDGE",
                              "An arrangement edge is shorter than the modeling tolerance");
    for (size_t i = 0; i < segments.size(); ++i)
        for (size_t j = i + 1; j < segments.size(); ++j)
            intersect(segments[i], segments[j]);
    std::map<Key, bool> graph;
    std::vector<std::vector<Id>> chains;
    for (auto &segment : segments) {
        std::sort(segment.cuts.begin(), segment.cuts.end(),
                  [](const auto &a, const auto &b) { return a.parameter < b.parameter; });
        chains.emplace_back();
        auto &chain = chains.back();
        for (const auto &cut : segment.cuts) {
            const auto id = result.surface.vertex(plane.world(cut.point));
            if (chain.empty() || chain.back() != id)
                chain.push_back(id);
        }
        for (size_t i = 1; i < chain.size(); ++i)
            graph[key(chain[i - 1], chain[i])] |= segment.inserted;
        if (graph.size() > pieceLimit)
            throw PlanarError("ARRANGEMENT_LIMIT", "Arrangement exceeds 16384 split edge pieces");
    }
    // Propagate boundary splits to every face, including faces outside this plane.
    for (auto &[id, face] : result.surface.faces)
        for (auto &loop : face.loops) {
            std::vector<Id> updated;
            for (size_t i = 0; i < loop.size(); ++i) {
                const auto a = loop[i], b = loop[(i + 1) % loop.size()];
                auto found = sourceSegments.find(key(a, b));
                if (found == sourceSegments.end())
                    updated.push_back(a);
                else {
                    auto chain = chains[found->second];
                    // Source edges use canonical endpoints, so their chains have that direction.
                    if (a > b)
                        std::reverse(chain.begin(), chain.end());
                    if (chain.front() != a || chain.back() != b)
                        throw PlanarError("UNSTABLE_INTERSECTION",
                                          "Snapping would move an existing boundary endpoint");
                    updated.insert(updated.end(), chain.begin(), chain.end() - 1);
                }
            }
            loop = std::move(updated);
        }
    std::map<Id, std::vector<Id>> neighbors;
    for (const auto &[edge, isNew] : graph) {
        neighbors[edge[0]].push_back(edge[1]);
        neighbors[edge[1]].push_back(edge[0]);
    }
    // Iterative bridge search: open branches do not become spurious face boundaries.
    std::map<Id, size_t> discovery, low, index;
    std::map<Id, Id> parent;
    size_t time = 0;
    std::set<Key> bridges;
    for (const auto &[start, adjacent] : neighbors) {
        if (discovery.contains(start))
            continue;
        std::vector<Id> stack{start};
        discovery[start] = low[start] = ++time;
        while (!stack.empty()) {
            const auto current = stack.back();
            if (index[current] < neighbors[current].size()) {
                const auto next = neighbors[current][index[current]++];
                if (!discovery.contains(next)) {
                    parent[next] = current;
                    discovery[next] = low[next] = ++time;
                    stack.push_back(next);
                } else if (!parent.contains(current) || next != parent.at(current))
                    low[current] = std::min(low[current], discovery[next]);
            } else {
                stack.pop_back();
                if (parent.contains(current)) {
                    const auto p = parent.at(current);
                    if (low[current] > discovery[p])
                        bridges.insert(key(current, p));
                    low[p] = std::min(low[p], low[current]);
                }
            }
        }
    }
    for (auto &[id, adjacent] : neighbors) {
        std::erase_if(adjacent, [&](Id other) { return bridges.contains(key(id, other)); });
        const auto center = plane.project(result.surface.vertices.at(id));
        std::sort(adjacent.begin(), adjacent.end(), [&](Id a, Id b) {
            const auto pa = plane.project(result.surface.vertices.at(a)) - center,
                       pb = plane.project(result.surface.vertices.at(b)) - center;
            const auto aa = std::atan2(pa.y, pa.x), ab = std::atan2(pb.y, pb.x);
            return aa == ab ? a < b : aa < ab;
        });
    }
    std::set<Key> visited;
    std::vector<std::vector<Id>> cycles;
    std::vector<Path64> paths;
    std::vector<double> areas;
    for (const auto &[start, adjacent] : neighbors)
        for (auto next : adjacent) {
            const Key first{start, next};
            if (visited.contains(first))
                continue;
            auto half = first;
            std::vector<Id> cycle;
            do {
                if (!visited.insert(half).second)
                    throw PlanarError("INVALID_ARRANGEMENT",
                                      "Directed loop traversal did not close consistently");
                cycle.push_back(half[0]);
                const auto &around = neighbors.at(half[1]);
                const auto it = std::find(around.begin(), around.end(), half[0]);
                if (it == around.end())
                    throw PlanarError("INVALID_ARRANGEMENT", "Missing reciprocal adjacency");
                const auto previous =
                    (size_t(it - around.begin()) + around.size() - 1) % around.size();
                half = {half[1], around[previous]};
                if (cycle.size() > pieceLimit * 2)
                    throw PlanarError("ARRANGEMENT_LIMIT",
                                      "Loop traversal exceeds arrangement budget");
            } while (half != first);
            auto polygon = path(cycle, result.surface, plane);
            const auto area = Area(polygon);
            if (area <= 1)
                continue; // Clockwise unbounded walks and zero-area remnants are not faces.
            if (std::set<Id>(cycle.begin(), cycle.end()).size() != cycle.size())
                throw PlanarError(
                    "TOUCHING_BOUNDARIES",
                    "A bounded region has a touching or repeated boundary; separate the outlines");
            if (cycles.size() >= 1024)
                throw PlanarError("ARRANGEMENT_LIMIT", "Arrangement exceeds 1024 bounded regions");
            cycles.push_back(std::move(cycle));
            paths.push_back(std::move(polygon));
            areas.push_back(area);
        }
    std::vector<int> parents(cycles.size(), -1);
    for (size_t i = 0; i < cycles.size(); ++i)
        for (size_t j = 0; j < cycles.size(); ++j) {
            if (i == j || areas[j] <= areas[i] ||
                (parents[i] >= 0 && areas[parents[i]] <= areas[j]))
                continue;
            bool inside = true;
            for (const auto &point : paths[i])
                if (PointInPolygon(point, paths[j]) != PointInPolygonResult::IsInside) {
                    inside = false;
                    break;
                }
            if (inside)
                parents[i] = int(j);
        }
    auto priorFaces = result.surface.faces;
    for (const auto &[id, paths] : sourceFaces)
        result.surface.faces.erase(id);
    Surface scratch;
    scratch.vertices = result.surface.vertices;
    for (size_t i = 0; i < cycles.size(); ++i) {
        Face candidate{0, {cycles[i]}};
        for (size_t child = 0; child < cycles.size(); ++child)
            if (parents[child] == int(i)) {
                auto hole = cycles[child];
                std::reverse(hole.begin(), hole.end());
                candidate.loops.push_back(std::move(hole));
            }
        scratch.faces[0] = candidate;
        const auto triangles = scratch.triangulate(0);
        const auto sample =
            integer(plane.project((triangles[0].a + triangles[0].b + triangles[0].c) * (1.0 / 3)));
        std::vector<Id> covering;
        bool inHole = false;
        for (const auto &[id, outline] : sourceFaces) {
            bool inside = PointInPolygon(sample, outline[0]) == PointInPolygonResult::IsInside;
            for (size_t h = 1; h < outline.size(); ++h)
                if (PointInPolygon(sample, outline[h]) != PointInPolygonResult::IsOutside) {
                    inside = false;
                    inHole = true;
                }
            if (inside)
                covering.push_back(id);
        }
        if (covering.size() > 1)
            throw PlanarError(
                "OVERLAPPING_FACES",
                "Existing coplanar faces overlap; resolve them before inserting edges");
        bool newBoundary = false;
        for (const auto &loop : candidate.loops)
            for (size_t j = 0; j < loop.size(); ++j)
                newBoundary |= graph.at(key(loop[j], loop[(j + 1) % loop.size()]));
        if (covering.empty() && ((inHole && !heal) || !newBoundary))
            continue;
        Id retained = 0;
        const auto shape = canonical(candidate);
        for (auto id : covering)
            if (canonical(priorFaces.at(id)) == shape)
                retained = id;
        if (retained) {
            candidate = priorFaces.at(retained);
            candidate.id = retained;
        } else {
            if (result.surface.nextId == UINT64_MAX)
                throw PlanarError("ID_EXHAUSTED", "Surface ID space exhausted");
            candidate.id = result.surface.nextId++;
            // Match an existing face's orientation when subdividing it.
            if (!covering.empty() && dot(source.normal(covering[0]), n) < 0)
                for (auto &loop : candidate.loops)
                    std::reverse(loop.begin(), loop.end());
        }
        for (auto id : covering)
            result.faces[id].push_back(candidate.id);
        result.surface.faces.emplace(candidate.id, std::move(candidate));
    }
    // Edges promoted into boundaries no longer need loose-wire records.
    std::set<Key> used;
    for (const auto &[id, face] : result.surface.faces)
        for (const auto &loop : face.loops)
            for (size_t i = 0; i < loop.size(); ++i)
                used.insert(key(loop[i], loop[(i + 1) % loop.size()]));
    result.surface.wires.clear();
    for (auto wire : source.wires)
        if (!sourceSegments.contains(key(wire[0], wire[1])))
            result.surface.wires.push_back(wire);
    for (const auto &[edge, isNew] : graph)
        if (!used.contains(edge))
            result.surface.wires.push_back(edge);
    auto wireKeys = [](const Surface &surface) {
        std::set<Key> keys;
        for (auto wire : surface.wires)
            keys.insert(key(wire[0], wire[1]));
        return keys;
    };
    if (result.surface.vertices == source.vertices && result.surface.faces == source.faces &&
        wireKeys(result.surface) == wireKeys(source))
        result.surface = source;
    result.surface.validate();
    for (const auto &[id, descendants] : result.faces) {
        double area = 0;
        for (auto child : descendants)
            area += result.surface.area(child);
        const auto expected = source.area(id);
        if (std::abs(area - expected) > std::max(1e-8, expected * 1e-8))
            throw PlanarError("AREA_MISMATCH",
                              "Planar insertion would change the existing face coverage");
    }
    return result;
}
} // namespace
PlanarResult insertPlanarEdges(const Surface &source, Vec3 origin, Vec3 normal,
                               const std::vector<std::array<Vec3, 2>> &edges, bool heal) {
    try {
        return build(source, origin, normal, edges, heal);
    } catch (const PlanarError &) {
        throw;
    } catch (const std::exception &error) {
        throw PlanarError("INVALID_TOPOLOGY", error.what());
    }
}
} // namespace sketchy
