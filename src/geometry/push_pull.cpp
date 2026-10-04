#include "geometry/push_pull.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <set>
namespace sketchy {
namespace {
using namespace Clipper2Lib;
constexpr double scale = 1e7;
struct Plane {
    Vec3 origin, n, u, v;
    Point64 project(Vec3 p) const {
        auto d = p - origin;
        return {std::llround(dot(d, u) * scale), std::llround(dot(d, v) * scale)};
    }
    Vec3 point(Point64 p) const {
        return origin + u * (double(p.x) / scale) + v * (double(p.y) / scale);
    }
};
Plane plane(const Surface &s, Id face) {
    auto n = s.normal(face), o = s.vertices.at(s.faces.at(face).loops[0][0]);
    auto u = normalized(cross(std::abs(n.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0}, n));
    return {o, n, u, cross(n, u)};
}
Paths64 paths(const Surface &s, Id face, const Plane &p) {
    Paths64 result;
    for (const auto &loop : s.faces.at(face).loops) {
        Path64 outline;
        for (auto id : loop)
            outline.push_back(p.project(s.vertices.at(id)));
        if (IsPositive(outline) != result.empty())
            std::reverse(outline.begin(), outline.end());
        result.push_back(std::move(outline));
    }
    return result;
}
bool inside(Point64 p, const Paths64 &profile) {
    if (PointInPolygon(p, profile[0]) != PointInPolygonResult::IsInside)
        return false;
    for (size_t h = 1; h < profile.size(); ++h)
        if (PointInPolygon(p, profile[h]) != PointInPolygonResult::IsOutside)
            return false;
    return true;
}
std::vector<Vec3> clip(const std::vector<Vec3> &polygon, Vec3 origin, Vec3 n, double bound,
                       bool above) {
    std::vector<Vec3> result;
    for (size_t i = 0; i < polygon.size(); ++i) {
        auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        auto da = dot(a - origin, n) - bound, db = dot(b - origin, n) - bound;
        bool ia = above ? da >= 0 : da <= 0, ib = above ? db >= 0 : db <= 0;
        if (ia)
            result.push_back(a);
        if (ia != ib)
            result.push_back(a + (b - a) * (da / (da - db)));
    }
    return result;
}
void collisionCheck(const Surface &s, Id face, const Plane &p, const Paths64 &profile,
                    double distance) {
    size_t budget = 1000000;
    const auto low = distance < 0 ? distance - tolerance : tolerance;
    const auto high = distance > 0 ? distance + tolerance : -tolerance;
    if (high <= low)
        throw std::runtime_error("Push/pull distance must exceed twice the modeling tolerance");
    for (const auto &[id, record] : s.faces) {
        if (id == face)
            continue;
        bool destinationPlane = true;
        for (const auto &loop : record.loops)
            for (auto v : loop)
                if (std::abs(dot(s.vertices.at(v) - p.origin, p.n) - distance) > tolerance)
                    destinationPlane = false;
        // Parallel destination faces are classified separately as an opening or
        // an invalid overlap. Other contacts at the cap plane must reject too.
        if (destinationPlane)
            continue;
        for (const auto &t : s.triangulate(id)) {
            auto polygon = clip({t.a, t.b, t.c}, p.origin, p.n, low, true);
            polygon = clip(polygon, p.origin, p.n, high, false);
            if (polygon.empty())
                continue;
            if (polygon.size() * profile.size() > budget)
                throw std::runtime_error("Push/pull intersection budget exceeded");
            budget -= polygon.size() * profile.size();
            Path64 projected;
            for (auto point : polygon)
                projected.push_back(p.project(point));
            if (std::abs(Area(projected)) >= 1) {
                if (std::abs(Area(Intersect(Paths64{projected}, profile, FillRule::NonZero))) >= 1)
                    throw std::runtime_error(
                        "Push/pull would cross an existing face before its destination");
            } else {
                // A face parallel to the sweep projects to a line. Clip it as an
                // open path and test strict interior, allowing coincident boundaries.
                Clipper64 engine;
                engine.AddOpenSubject({projected});
                engine.AddClip(profile);
                Paths64 closed, open;
                engine.Execute(ClipType::Intersection, FillRule::NonZero, closed, open);
                for (const auto &line : open)
                    for (size_t i = 1; i < line.size(); ++i) {
                        Point64 middle{(line[i - 1].x + line[i].x) / 2,
                                       (line[i - 1].y + line[i].y) / 2};
                        if (inside(middle, profile))
                            throw std::runtime_error("Push/pull intersects an interior wall");
                    }
            }
        }
    }
}
// Translate a complete prismatic cap and its existing side endpoints. Reusing
// records avoids overlapping walls on inward box/concave/holed-prism edits.
bool moveCap(const Surface &source, Id face, Vec3 delta, Surface &result) {
    std::set<Id> cap, neighbors;
    for (const auto &loop : source.faces.at(face).loops)
        cap.insert(loop.begin(), loop.end());
    for (const auto &edge : source.edges()) {
        if (!cap.contains(edge.a) || !cap.contains(edge.b))
            continue;
        if (edge.faces.size() != 2 ||
            std::find(edge.faces.begin(), edge.faces.end(), face) == edge.faces.end())
            return false;
        for (auto id : edge.faces)
            if (id != face)
                neighbors.insert(id);
    }
    if (neighbors.empty())
        return false;
    auto direction = normalized(delta);
    for (const auto &[id, record] : source.faces) {
        if (id == face)
            continue;
        bool touches = false;
        for (const auto &loop : record.loops)
            for (auto v : loop)
                touches |= cap.contains(v);
        if (!touches)
            continue;
        if (!neighbors.contains(id) || record.loops.size() != 1 || record.loops[0].size() != 4)
            return false;
        const auto &loop = record.loops[0];
        int count = 0, vertical = 0;
        for (size_t i = 0; i < loop.size(); ++i) {
            auto a = loop[i], b = loop[(i + 1) % loop.size()];
            count += cap.contains(a);
            if (cap.contains(a) != cap.contains(b)) {
                auto segment = source.vertices.at(b) - source.vertices.at(a);
                if (length(cross(segment, direction)) > tolerance)
                    return false;
                ++vertical;
            }
        }
        if (count != 2 || vertical != 2)
            return false;
    }
    for (auto wire : source.wires)
        if (cap.contains(wire[0]) || cap.contains(wire[1]))
            return false;
    for (auto id : cap)
        result.vertices.at(id) = source.vertices.at(id) + delta;
    result.validate();
    return true;
}
void checkCoplanarOverlap(const Surface &s) {
    size_t budget = 1000000;
    for (auto a = s.faces.begin(); a != s.faces.end(); ++a) {
        const auto p = plane(s, a->first);
        for (auto b = std::next(a); b != s.faces.end(); ++b) {
            if (!budget--)
                throw std::runtime_error("Push/pull face-pair budget exceeded");
            bool coplanar = true;
            for (const auto &loop : b->second.loops)
                for (auto id : loop)
                    if (std::abs(dot(s.vertices.at(id) - p.origin, p.n)) > tolerance)
                        coplanar = false;
            if (coplanar && std::abs(Area(Intersect(paths(s, a->first, p), paths(s, b->first, p),
                                                    FillRule::NonZero))) >= 1)
                throw std::runtime_error("Push/pull would leave overlapping coplanar faces");
        }
    }
}
std::vector<Id> difference(Surface &s, Id old, const Plane &p, const Paths64 &cut) {
    Clipper64 engine;
    engine.AddSubject(paths(s, old, p));
    engine.AddClip(cut);
    PolyTree64 tree;
    if (!engine.Execute(ClipType::Difference, FillRule::NonZero, tree))
        throw std::runtime_error("Opposite face cut failed");
    const bool reverse = dot(s.normal(old), p.n) < 0;
    s.faces.erase(old);
    std::vector<Id> created;
    size_t budget = 1000000;
    auto collect = [&](auto &&self, const PolyPath64 &node) -> void {
        for (const auto &child : node) {
            if (!child->IsHole()) {
                std::vector<std::vector<Id>> loops;
                auto convert = [&](const Path64 &path) {
                    std::vector<Id> loop;
                    for (size_t i = 0; i < path.size(); ++i) {
                        auto a = p.point(path[i]), b = p.point(path[(i + 1) % path.size()]),
                             d = b - a;
                        const auto len = length(d);
                        std::vector<std::pair<double, Id>> points{{0, s.vertex(a)}};
                        if (s.vertices.size() > budget)
                            throw std::runtime_error("Opposite boundary match budget exceeded");
                        budget -= s.vertices.size();
                        for (auto [id, point] : s.vertices) {
                            auto t = dot(point - a, d) / dot(d, d);
                            if (t > tolerance / len && t < 1 - tolerance / len &&
                                length(cross(point - a, d)) / len <= tolerance)
                                points.emplace_back(t, id);
                        }
                        std::sort(points.begin(), points.end());
                        for (auto [t, id] : points)
                            loop.push_back(id);
                    }
                    if (reverse)
                        std::reverse(loop.begin(), loop.end());
                    loops.push_back(std::move(loop));
                };
                convert(child->Polygon());
                for (const auto &hole : *child)
                    convert(hole->Polygon());
                created.push_back(s.addFaceIds(std::move(loops)));
            }
            self(self, *child);
        }
    };
    collect(collect, tree);
    return created;
}
} // namespace
TopologyEdit pushPull(const Surface &source, Id face, double distance, bool newFace) {
    source.validate();
    if (!std::isfinite(distance) || std::abs(distance) <= 2 * tolerance)
        throw std::runtime_error(
            "Push/pull distance must be finite and exceed twice the modeling tolerance");
    if (!source.faces.contains(face))
        throw std::runtime_error("Selected face does not exist");
    if (source.faces.size() > 1000 || source.vertices.size() > 10000)
        throw std::runtime_error("Push/pull exceeds the 1000-face/10000-vertex context limit");
    TopologyEdit result{source, {}, {}, {}};
    if (source.faces.size() == 1 && source.wires.empty()) {
        auto top = result.surface.extrude(face, distance);
        result.faces[face] = {face, top};
        return result;
    }
    const auto p = plane(source, face);
    const auto profile = paths(source, face, p);
    const auto delta = p.n * distance;
    for (const auto &loop : source.faces.at(face).loops)
        for (auto v : loop)
            checkPoint(source.vertices.at(v) + delta);
    // Non-manifold source boundaries do not define an unambiguous sweep.
    for (const auto &edge : source.edges())
        if (std::find(edge.faces.begin(), edge.faces.end(), face) != edge.faces.end() &&
            edge.faces.size() > 2)
            throw std::runtime_error(
                "Push/pull requires at most two incident faces per selected edge");
    collisionCheck(source, face, p, profile, distance);
    bool isolated = true;
    for (const auto &edge : source.edges())
        if (std::find(edge.faces.begin(), edge.faces.end(), face) != edge.faces.end() &&
            edge.faces.size() != 1)
            isolated = false;
    if (isolated) {
        Surface component = source;
        component.faces.clear();
        component.faces.emplace(face, source.faces.at(face));
        component.wires.clear();
        const auto top = component.extrude(face, distance);
        for (const auto &[id, record] : source.faces)
            if (id != face)
                component.faces.emplace(id, record);
        component.wires = source.wires;
        checkCoplanarOverlap(component);
        result.surface = std::move(component);
        result.faces[face] = {face, top};
        return result;
    }

    Id opposite = 0;
    for (const auto &[id, record] : source.faces) {
        if (id == face)
            continue;
        bool atEnd = true;
        for (const auto &loop : record.loops)
            for (auto v : loop)
                if (std::abs(dot(source.vertices.at(v) - p.origin, p.n) - distance) > tolerance)
                    atEnd = false;
        if (!atEnd)
            continue;
        auto overlap = std::abs(Area(Intersect(profile, paths(source, id, p), FillRule::NonZero)));
        if (overlap < 1)
            continue;
        if (distance >= 0 || dot(source.normal(id), p.n) > -1 + 1e-8 || opposite ||
            std::abs(overlap - std::abs(Area(profile))) >
                std::max(1.0, std::abs(Area(profile)) * 1e-8))
            throw std::runtime_error(
                "Destination must be one opposing face covering the complete pushed profile");
        opposite = id;
    }
    if (opposite && newFace)
        throw std::runtime_error(
            "Create new face cannot terminate on an opposing face; disable it to cut an opening");
    if (!newFace && !opposite && moveCap(source, face, delta, result.surface)) {
        checkCoplanarOverlap(result.surface);
        return result;
    }
    auto loops = source.faces.at(face).loops;
    // Canonical outer and hole orientation follows the selected face normal.
    for (size_t l = 0; l < loops.size(); ++l) {
        Path64 outline;
        for (auto v : loops[l])
            outline.push_back(p.project(source.vertices.at(v)));
        if (IsPositive(outline) != (l == 0))
            std::reverse(loops[l].begin(), loops[l].end());
    }
    auto top = loops;
    for (auto &loop : top)
        for (auto &v : loop)
            v = result.surface.vertex(source.vertices.at(v) + delta);
    if (!newFace)
        result.surface.faces.erase(face);
    if (opposite) {
        auto destination = p;
        destination.origin = destination.origin + delta;
        result.faces[opposite] = difference(result.surface, opposite, destination, profile);
        result.faces[face] = {};
    } else if (newFace) {
        const auto cap = result.surface.addFaceIds(top);
        result.faces[face] = {face, cap};
    } else {
        result.surface.faces.emplace(face, Face{face, top});
    }
    for (size_t l = 0; l < loops.size(); ++l)
        for (size_t i = 0; i < loops[l].size(); ++i) {
            auto j = (i + 1) % loops[l].size();
            result.surface.addFaceIds({{loops[l][i], loops[l][j], top[l][j], top[l][i]}});
        }
    result.surface.validate();
    checkCoplanarOverlap(result.surface);
    // A completed opening must have matching radial boundaries on both sides.
    if (opposite)
        for (const auto &edge : result.surface.edges())
            if (edge.faces.size() != 2)
                throw std::runtime_error(
                    "Through opening would leave unmatched or non-manifold boundaries");
    return result;
}
} // namespace sketchy
