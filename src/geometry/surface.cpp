#include "geometry/surface.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <limits>
#include <set>

namespace sketchy {
Vec3 normalized(Vec3 v) {
    const auto n = length(v);
    if (!std::isfinite(n) || n < tolerance)
        throw std::runtime_error("Degenerate direction");
    return v * (1 / n);
}
void checkPoint(Vec3 v) {
    for (auto a : {v.x, v.y, v.z})
        if (!std::isfinite(a) || std::abs(a) > coordinateLimit)
            throw std::runtime_error("Coordinates must be finite and within one million meters");
}
Id Surface::vertex(Vec3 point) {
    checkPoint(point);
    for (const auto &[id, p] : vertices)
        if (length(p - point) < tolerance)
            return id;
    if (nextId == std::numeric_limits<Id>::max())
        throw std::runtime_error("ID space exhausted");
    const Id id = nextId++;
    vertices.emplace(id, point);
    return id;
}
Id Surface::addFace(const std::vector<std::vector<Vec3>> &loops) {
    // Work on a copy so failed triangulation never leaves orphan vertices.
    Surface staged = *this;
    std::vector<std::vector<Id>> ids;
    for (const auto &loop : loops) {
        ids.emplace_back();
        for (auto p : loop)
            ids.back().push_back(staged.vertex(p));
    }
    const auto id = staged.addFaceIds(std::move(ids));
    *this = std::move(staged);
    return id;
}
Id Surface::addFaceIds(std::vector<std::vector<Id>> loops) {
    if (nextId == std::numeric_limits<Id>::max())
        throw std::runtime_error("ID space exhausted");
    Id id = nextId;
    faces.emplace(id, Face{id, std::move(loops)});
    try {
        (void)triangulate(id);
    } catch (...) {
        faces.erase(id);
        throw;
    }
    ++nextId;
    return id;
}
Vec3 Surface::normal(Id id) const {
    const auto &loops = faces.at(id).loops;
    if (loops.empty() || loops[0].size() < 3)
        throw std::runtime_error("A face needs a closed loop of at least three vertices");
    Vec3 n{};
    const auto &loop = loops[0];
    const auto origin = vertices.at(loop[0]);
    for (size_t i = 0; i < loop.size(); ++i)
        n = n +
            cross(vertices.at(loop[i]) - origin, vertices.at(loop[(i + 1) % loop.size()]) - origin);
    // Newell's vector has area units. Comparing it with a linear tolerance
    // incorrectly rejected small valid faces (e.g. a 0.1 mm square).
    const auto magnitude = length(n);
    if (!std::isfinite(magnitude) || magnitude < 2 * tolerance * tolerance)
        throw std::runtime_error("Face area is below tolerance");
    return n * (1 / magnitude);
}
std::vector<Triangle> Surface::triangulate(Id id) const {
    using namespace Clipper2Lib;
    const auto &face = faces.at(id);
    auto n = normal(id);
    auto origin = vertices.at(face.loops[0][0]);
    auto u = normalized(vertices.at(face.loops[0][1]) - origin), v = cross(n, u);
    Paths64 paths;
    constexpr double scale = 1e7;
    for (const auto &loop : face.loops) {
        if (loop.size() < 3 || loop.size() > 10000)
            throw std::runtime_error("Invalid loop vertex count");
        std::set<Id> unique;
        Path64 path;
        for (Id vertexId : loop) {
            if (!unique.insert(vertexId).second)
                throw std::runtime_error("Repeated loop vertex");
            auto p = vertices.at(vertexId);
            checkPoint(p);
            auto d = p - origin;
            if (std::abs(dot(d, n)) > tolerance)
                throw std::runtime_error("Face is not planar");
            path.emplace_back(std::llround(dot(d, u) * scale), std::llround(dot(d, v) * scale));
        }
        if (std::abs(Area(path)) < 1)
            throw std::runtime_error("Face area is below tolerance");
        if (IsPositive(path) != (paths.empty()))
            std::reverse(path.begin(), path.end());
        paths.push_back(std::move(path));
    }
    // Reject intersecting outlines, touching holes and holes outside the outer loop.
    // Clipper triangulation detects crossing paths, while point containment enforces nesting.
    for (size_t h = 1; h < paths.size(); ++h) {
        for (auto p : paths[h])
            if (PointInPolygon(p, paths[0]) != PointInPolygonResult::IsInside)
                throw std::runtime_error("Hole must lie strictly inside the outer loop");
        for (size_t j = 1; j < h; ++j)
            if (PointInPolygon(paths[h][0], paths[j]) != PointInPolygonResult::IsOutside ||
                PointInPolygon(paths[j][0], paths[h]) != PointInPolygonResult::IsOutside)
                throw std::runtime_error("Nested or overlapping holes are unsupported");
    }
    Paths64 result;
    if (Triangulate(paths, result) != TriangulateResult::success)
        throw std::runtime_error("Intersecting or degenerate face outline");
    std::vector<Triangle> output;
    for (const auto &t : result) {
        if (t.size() != 3)
            throw std::runtime_error("Invalid triangulation result");
        auto point = [&](Point64 p) {
            return origin + u * (double(p.x) / scale) + v * (double(p.y) / scale);
        };
        Triangle tri{point(t[0]), point(t[1]), point(t[2]), id};
        if (dot(cross(tri.b - tri.a, tri.c - tri.a), n) < 0)
            std::swap(tri.b, tri.c);
        output.push_back(tri);
    }
    double expected = 0, actual = 0;
    for (const auto &p : paths)
        expected += Area(p) / (scale * scale);
    for (const auto &t : output)
        actual += length(cross(t.b - t.a, t.c - t.a)) * 0.5;
    if (output.empty() || std::abs(actual - expected) > std::max(1e-8, std::abs(expected) * 1e-8))
        throw std::runtime_error("Triangulation area mismatch");
    return output;
}
std::vector<Triangle> Surface::triangles() const {
    std::vector<Triangle> result;
    for (const auto &[id, f] : faces) {
        auto mesh = triangulate(id);
        result.insert(result.end(), mesh.begin(), mesh.end());
    }
    return result;
}
std::vector<Edge> Surface::edges() const {
    std::map<std::pair<Id, Id>, Edge> index;
    auto add = [&](Id a, Id b, Id face) {
        if (a > b)
            std::swap(a, b);
        auto &e = index[{a, b}];
        e.a = a;
        e.b = b;
        if (face)
            e.faces.push_back(face);
    };
    for (const auto &[id, face] : faces)
        for (const auto &l : face.loops)
            for (size_t i = 0; i < l.size(); ++i)
                add(l[i], l[(i + 1) % l.size()], id);
    for (auto e : wires)
        add(e[0], e[1], 0);
    std::vector<Edge> out;
    for (const auto &[key, e] : index)
        out.push_back(e);
    return out;
}
void Surface::validate() const {
    if (vertices.size() > 100000 || faces.size() > 100000)
        throw std::runtime_error("Surface exceeds editing limits");
    for (auto [id, p] : vertices) {
        if (id == 0 || id >= nextId || faces.contains(id))
            throw std::runtime_error("Invalid vertex identity");
        checkPoint(p);
    }
    for (const auto &[id, face] : faces) {
        if (id == 0 || id >= nextId || id != face.id)
            throw std::runtime_error("Invalid face identity");
        (void)triangulate(id);
    }
    for (auto e : wires)
        if (!vertices.contains(e[0]) || !vertices.contains(e[1]) || e[0] == e[1])
            throw std::runtime_error("Invalid wire edge");
}
double Surface::area(Id id) const {
    // Validate with the tessellator, but measure authoritative loop coordinates.
    // Independently quantized meshes can differ by a few ulps of the 1e-7 m
    // grid after a face is partitioned; their summed areas are not a reliable
    // conservation check for continuous pointer construction.
    (void)triangulate(id);
    double area = 0;
    const auto &loops = faces.at(id).loops;
    for (size_t i = 0; i < loops.size(); ++i) {
        const auto &loop = loops[i];
        const auto origin = vertices.at(loop.front());
        Vec3 sum{};
        for (size_t j = 0; j < loop.size(); ++j)
            sum = sum + cross(vertices.at(loop[j]) - origin,
                              vertices.at(loop[(j + 1) % loop.size()]) - origin);
        area += (i == 0 ? 1 : -1) * length(sum) * .5;
    }
    return area;
}

Id Surface::extrude(Id id, double distance) {
    if (!std::isfinite(distance) || std::abs(distance) < tolerance)
        throw std::runtime_error("Extrusion distance must be nonzero and finite");
    if (faces.size() != 1 || !wires.empty())
        throw std::runtime_error(
            "This spike extrudes isolated faces only; adjacent-face push/pull is not implemented");
    Surface staged = *this;
    auto n = normal(id);
    auto loops = faces.at(id).loops;
    // Orient every boundary consistently before generating side faces.
    for (size_t l = 0; l < loops.size(); ++l) {
        Vec3 sum{};
        auto o = vertices.at(loops[l][0]);
        for (size_t i = 0; i < loops[l].size(); ++i)
            sum = sum + cross(vertices.at(loops[l][i]) - o,
                              vertices.at(loops[l][(i + 1) % loops[l].size()]) - o);
        if ((dot(sum, n) > 0) != (l == 0))
            std::reverse(loops[l].begin(), loops[l].end());
    }
    if (distance < 0)
        for (auto &l : loops)
            std::reverse(l.begin(), l.end());
    auto base = loops, top = loops;
    for (auto &loop : top)
        for (auto &vertexId : loop)
            vertexId = staged.vertex(vertices.at(vertexId) + n * distance);
    for (auto &l : base)
        std::reverse(l.begin(), l.end());
    staged.faces.at(id).loops = base;
    const Id topId = staged.addFaceIds(top);
    for (size_t l = 0; l < loops.size(); ++l)
        for (size_t i = 0; i < loops[l].size(); ++i) {
            size_t j = (i + 1) % loops[l].size();
            staged.addFaceIds({{loops[l][i], loops[l][j], top[l][j], top[l][i]}});
        }
    staged.validate();
    *this = std::move(staged);
    return topId;
}
void Surface::translate(Vec3 delta) {
    checkPoint(delta);
    for (auto [id, p] : vertices)
        checkPoint(p + delta);
    for (auto &[id, p] : vertices)
        p = p + delta;
}
} // namespace sketchy
