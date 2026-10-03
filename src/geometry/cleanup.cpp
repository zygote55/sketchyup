#include "geometry/cleanup.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <set>
namespace sketchy {
namespace {
using Key = std::array<Id, 2>;
Key key(Id a, Id b) { return {std::min(a, b), std::max(a, b)}; }
std::set<Key> boundaries(const Surface &source, const std::set<Id> &faces) {
    std::set<Key> result;
    for (auto id : faces)
        for (const auto &loop : source.faces.at(id).loops)
            for (size_t i = 0; i < loop.size(); ++i)
                result.insert(key(loop[i], loop[(i + 1) % loop.size()]));
    return result;
}
void retainWires(Surface &surface, const std::set<Key> &removedBoundaries, Key erased = {0, 0}) {
    std::set<Key> supported, wires;
    for (const auto &[id, face] : surface.faces)
        for (const auto &loop : face.loops)
            for (size_t i = 0; i < loop.size(); ++i)
                supported.insert(key(loop[i], loop[(i + 1) % loop.size()]));
    for (auto edge : surface.wires)
        if (key(edge[0], edge[1]) != erased)
            wires.insert(key(edge[0], edge[1]));
    for (auto edge : removedBoundaries)
        if (edge != erased && !supported.contains(edge))
            wires.insert(edge);
    surface.wires.assign(wires.begin(), wires.end());
}
} // namespace
TopologyEdit eraseFace(const Surface &source, Id face) {
    source.validate();
    if (!source.faces.contains(face))
        throw std::runtime_error("Face does not exist in this editing context");
    TopologyEdit result{source, {{face, {}}}, {}, {}};
    const auto outline = boundaries(source, {face});
    result.surface.faces.erase(face);
    retainWires(result.surface, outline);
    result.surface.validate();
    return result;
}
TopologyEdit eraseEdge(const Surface &source, const Topology &topology, Id edgeId) {
    using namespace Clipper2Lib;
    source.validate();
    topology.validate(source);
    if (!topology.edges.contains(edgeId))
        throw std::runtime_error("Edge does not exist in this editing context");
    const auto edge = topology.edges.at(edgeId);
    const auto adjacency = topology.adjacency(source);
    std::set<Id> incident;
    for (const auto &use : adjacency.edgeFaces.at(edgeId))
        incident.insert(use.face);
    TopologyEdit result{source, {}, {}, {}};
    const auto outline = boundaries(source, incident);
    bool join = incident.size() == 2;
    Vec3 origin{}, n{}, u{}, v{};
    if (join) {
        const auto first = *incident.begin();
        n = source.normal(first);
        origin = source.vertices.at(source.faces.at(first).loops[0][0]);
        const auto axis = std::abs(n.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        u = normalized(cross(axis, n));
        v = cross(n, u);
        for (auto id : incident)
            for (const auto &loop : source.faces.at(id).loops)
                for (auto point : loop)
                    if (std::abs(dot(source.vertices.at(point) - origin, n)) > tolerance)
                        join = false;
    }
    for (auto id : incident) {
        result.surface.faces.erase(id);
        result.faces[id] = {};
    }
    if (join) {
        constexpr double scale = 1e7;
        Paths64 paths;
        double expected = 0;
        for (auto id : incident) {
            expected += source.area(id);
            bool outer = true;
            for (const auto &loop : source.faces.at(id).loops) {
                Path64 path;
                for (auto vertex : loop) {
                    const auto p = source.vertices.at(vertex) - origin;
                    path.emplace_back(std::llround(dot(p, u) * scale),
                                      std::llround(dot(p, v) * scale));
                }
                if (IsPositive(path) != outer)
                    std::reverse(path.begin(), path.end());
                outer = false;
                paths.push_back(std::move(path));
            }
        }
        Clipper64 clipper;
        clipper.AddSubject(paths);
        PolyTree64 tree;
        if (!clipper.Execute(ClipType::Union, FillRule::NonZero, tree))
            throw std::runtime_error("Coplanar face merge failed");
        std::vector<Id> created;
        size_t boundaryBudget = 1000000;
        auto collect = [&](auto &&self, const PolyPath64 &node) -> void {
            for (const auto &child : node) {
                if (child->IsHole()) {
                    self(self, *child);
                    continue;
                }
                std::vector<std::vector<Id>> loops;
                auto convert = [&](const Path64 &path) {
                    std::vector<Id> loop;
                    for (size_t i = 0; i < path.size(); ++i) {
                        if (source.vertices.size() > boundaryBudget)
                            throw std::runtime_error(
                                "Merged boundary exceeds the one-million vertex-match budget");
                        boundaryBudget -= source.vertices.size();
                        const auto p = path[i], q = path[(i + 1) % path.size()];
                        const auto a =
                            origin + u * (double(p.x) / scale) + v * (double(p.y) / scale);
                        const auto b = origin + u * (double(q.x) / scale) +
                                       v * (double(q.y) / scale),
                                   delta = b - a;
                        const auto magnitude = length(delta);
                        if (magnitude < tolerance)
                            throw std::runtime_error("Merged boundary is below tolerance");
                        std::vector<std::pair<double, Id>> points{{0, result.surface.vertex(a)}};
                        // Retain existing collinear boundary vertices, so adjacent faces never
                        // acquire a T junction when Clipper simplifies the union contour.
                        for (const auto &[id, point] : source.vertices) {
                            const auto d = point - a;
                            const auto t = dot(d, delta) / dot(delta, delta);
                            if (t > tolerance / magnitude && t < 1 - tolerance / magnitude &&
                                length(cross(d, delta)) / magnitude <= tolerance)
                                points.emplace_back(t, id);
                        }
                        std::sort(points.begin(), points.end());
                        for (const auto &[t, id] : points)
                            if (loop.empty() || loop.back() != id)
                                loop.push_back(id);
                    }
                    loops.push_back(std::move(loop));
                };
                convert(child->Polygon());
                for (const auto &hole : *child)
                    convert(hole->Polygon());
                const auto face = result.surface.addFaceIds(std::move(loops));
                created.push_back(face);
                self(self, *child);
            }
        };
        collect(collect, tree);
        if (created.size() != 1)
            throw std::runtime_error("Erased coplanar divider did not produce one joined face");
        const auto actual = result.surface.area(created.front());
        if (std::abs(actual - expected) > std::max(1e-8, expected * 1e-8))
            throw std::runtime_error("Face merge would change coverage or merge overlapping faces");
        for (auto id : incident)
            result.faces[id] = created;
        // Internal shared boundaries disappear with the joined faces. Unrelated
        // explicit wire records survive except for the selected erased edge.
        retainWires(result.surface, {}, key(edge.a, edge.b));
    } else
        retainWires(result.surface, outline, key(edge.a, edge.b));
    result.surface.validate();
    return result;
}
TopologyEdit cleanupCoincident(const Surface &source, const Topology &topology) {
    source.validate();
    topology.validate(source);
    using Cell = std::array<std::int64_t, 3>;
    std::map<Cell, std::vector<Id>> cells;
    std::map<Id, Id> remap;
    TopologyEdit result{source, {}, {}, {}};
    for (const auto &[id, point] : source.vertices) {
        Cell cell{std::int64_t(std::floor(point.x / tolerance)),
                  std::int64_t(std::floor(point.y / tolerance)),
                  std::int64_t(std::floor(point.z / tolerance))};
        Id canonical = id;
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z) {
                    const auto found = cells.find({cell[0] + x, cell[1] + y, cell[2] + z});
                    if (found == cells.end())
                        continue;
                    for (auto candidate : found->second)
                        if (length(source.vertices.at(candidate) - point) <= tolerance)
                            canonical = std::min(canonical, candidate);
                }
        remap[id] = canonical;
        if (canonical == id)
            cells[cell].push_back(id);
        else {
            result.surface.vertices.erase(id);
            result.vertices[id] = {canonical};
        }
    }
    for (auto &[id, face] : result.surface.faces)
        for (auto &loop : face.loops) {
            std::vector<Id> mapped;
            for (auto vertex : loop)
                if (mapped.empty() || mapped.back() != remap.at(vertex))
                    mapped.push_back(remap.at(vertex));
            if (mapped.size() > 1 && mapped.front() == mapped.back())
                mapped.pop_back();
            loop = std::move(mapped);
        }
    std::set<Key> wires;
    for (auto edge : source.wires) {
        auto a = remap.at(edge[0]), b = remap.at(edge[1]);
        if (a != b)
            wires.insert(key(a, b));
    }
    result.surface.wires.assign(wires.begin(), wires.end());
    result.surface.validate();
    const auto updated = Topology::rebuild(result.surface, topology);
    std::map<Key, Id> lookup;
    for (const auto &[id, edge] : updated.edges)
        lookup[{edge.a, edge.b}] = id;
    for (const auto &[id, edge] : topology.edges) {
        const auto a = remap.at(edge.a), b = remap.at(edge.b);
        if (a == b)
            result.edges[id] = {};
        else if (lookup.at(key(a, b)) != id)
            result.edges[id] = {lookup.at(key(a, b))};
    }
    return result;
}
} // namespace sketchy
