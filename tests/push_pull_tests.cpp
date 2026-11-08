#include "core/model.hpp"
#include "geometry/push_pull.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <set>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
double volume(const Surface &s) {
    double result = 0;
    for (auto t : s.triangles())
        result += dot(t.a, cross(t.b, t.c)) / 6;
    return result;
}
void closed(const Surface &s, double expected) {
    s.validate();
    for (auto edge : s.edges())
        check(edge.faces.size() == 2, "Closed radial incidence");
    check(std::abs(volume(s) - expected) < 1e-6, "Expected oriented volume");
}
Id upward(const Surface &s) {
    for (auto [id, f] : s.faces)
        if (s.normal(id).z > .99)
            return id;
    throw std::runtime_error("No upward face");
}
Document box() {
    Document d;
    auto b = d.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
    d.extrude(b, d.bodies().at(b)->surface.faces.begin()->first, 2);
    return d;
}
Id patch(Document &d) {
    d.insertEdges(1, {0, 0, 2}, {0, 0, 1},
                  {{{{1, 1, 2}, {3, 1, 2}}},
                   {{{3, 1, 2}, {3, 3, 2}}},
                   {{{3, 3, 2}, {1, 3, 2}}},
                   {{{1, 3, 2}, {1, 1, 2}}}});
    for (auto [id, f] : d.bodies().at(1)->surface.faces)
        if (d.bodies().at(1)->surface.normal(id).z > .99 &&
            std::abs(d.bodies().at(1)->surface.area(id) - 4) < 1e-8)
            return id;
    throw std::runtime_error("No patch face");
}
bool same(const Body &a, const Body &b) {
    return a.surface.vertices == b.surface.vertices && a.surface.faces == b.surface.faces &&
           a.surface.wires == b.surface.wires && a.topology.edges == b.topology.edges;
}
// The push/pull before R117 (origin/main 0038456f, src/geometry/push_pull.cpp), copied unchanged
// except for its namespace. The randomized comparison below runs it as the reference (D-106).
namespace reference {
using namespace sketchy;

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
} // namespace reference

namespace {
struct Outcome {
    bool accepted{};
    TopologyEdit edit;
    std::string message;
};
template <class Run> Outcome outcome(Run run) {
    try {
        return {true, run(), {}};
    } catch (const std::exception &error) {
        return {false, {}, error.what()};
    }
}
// A convex prism or a star prism (concave), with a random height.
Surface randomBody(std::mt19937 &rng) {
    const int sides = 3 + int(rng() % 6);
    const bool star = rng() % 2 == 0;
    std::vector<Vec3> loop;
    for (int i = 0; i < (star ? 2 * sides : sides); ++i) {
        const double angle = 2 * std::numbers::pi * i / (star ? 2 * sides : sides);
        const double radius = star && i % 2 ? 0.45 : 1.0;
        loop.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    Surface surface;
    const Id face = surface.addFace({loop});
    surface.extrude(face, 0.5 + double(rng() % 8) / 4);
    return surface;
}
} // namespace
// R117 (D-106): the coplanar check, the opposite-face cut and the bounding-box culling were
// rewritten for large bodies. Each push/pull on a random small body must give the result the
// reference gives, or refuse with the same message.
void matchesReference() {
    std::mt19937 rng(20261107);
    size_t accepted = 0, refused = 0;
    for (int round = 0; round < 80; ++round) {
        auto body = randomBody(rng);
        for (int push = 0; push < 6; ++push) {
            std::vector<Id> faces;
            for (const auto &[id, record] : body.faces)
                faces.push_back(id);
            const Id face = faces[rng() % faces.size()];
            const double distance = (0.1 + 0.1 * double(rng() % 15)) * (rng() % 2 ? 1 : -1);
            const bool newFace = rng() % 3 == 0;
            const auto current =
                outcome([&] { return sketchy::pushPull(body, face, distance, newFace); });
            const auto before =
                outcome([&] { return reference::pushPull(body, face, distance, newFace); });
            check(current.accepted == before.accepted,
                  "Push/pull accepts the same sweeps as before R117");
            if (current.accepted) {
                check(current.edit.surface == before.edit.surface &&
                          current.edit.faces == before.edit.faces &&
                          current.edit.vertices == before.edit.vertices &&
                          current.edit.edges == before.edit.edges,
                      "Push/pull results match before R117");
                body = current.edit.surface;
                ++accepted;
            } else {
                check(current.message == before.message, "Push/pull refusals match before R117");
                ++refused;
            }
        }
    }
    check(accepted > 50 && refused > 50, "The random sweeps exercise both outcomes");
}
// Before R117 a body above 1,000 faces was refused by the context limit. The body limit alone
// applies now, so a 1,000-sided prism pushes its cap.
void largeBodyAccepted() {
    Surface prism;
    std::vector<Vec3> loop;
    for (int i = 0; i < 1000; ++i) {
        const double angle = 2 * std::numbers::pi * i / 1000;
        loop.push_back({std::cos(angle), std::sin(angle), 0});
    }
    prism.extrude(prism.addFace({loop}), 1);
    check(prism.faces.size() == 1002, "A 1000-sided prism has 1,002 faces");
    const auto cap = upward(prism);
    const auto pushed = sketchy::pushPull(prism, cap, 0.5);
    pushed.surface.validate();
    check(pushed.surface.faces.size() == 1002, "The pushed prism keeps its faces");
    const double area = 0.5 * 1000 * std::sin(2 * std::numbers::pi / 1000);
    // The cap is triangulated on a 1e-7 grid, so 1,000 sides carry about 1e-5 of rounding.
    check(std::abs(volume(pushed.surface) - volume(prism) - 0.5 * area) < 1e-4,
          "The large prism gains the swept volume");
    const auto before = outcome([&] { return reference::pushPull(prism, cap, 0.5, false); });
    check(!before.accepted && before.message.find("context limit") != std::string::npos,
          "The reference refused a body above 1,000 faces");
}

int main() {
    try {
        matchesReference();
        largeBodyAccepted();
        auto retained = box();
        const auto retainedBefore = retained.bodies().at(1);
        const auto retainedFace = upward(retainedBefore->surface);
        const auto retainedChange = retained.pushPull(1, retainedFace, 1, true);
        const auto &retainedAfter = retained.bodies().at(1)->surface;
        retainedAfter.validate();
        check(retainedAfter.faces.at(retainedFace) ==
                  retainedBefore->surface.faces.at(retainedFace),
              "New-face push/pull retains the original cap and vertex identities");
        check(retainedAfter.faces.size() == retainedBefore->surface.faces.size() + 5,
              "New-face push/pull adds a cap and four sides without moving old walls");
        for (const auto &[id, point] : retainedBefore->surface.vertices)
            check(retainedAfter.vertices.at(id) == point,
                  "Existing vertices stay fixed in new-face mode");
        const auto caps = retainedChange.at(1).faces.descendants.at(retainedFace);
        check(caps.size() == 2, "Retained and new cap identities are reported");
        const auto tip = caps[0] == retainedFace ? caps[1] : caps[0];
        retained.pushPull(1, tip, 1, true);
        check(retained.bodies().at(1)->surface.faces.size() == retainedAfter.faces.size() + 5,
              "New cap can be extended again while keeping an intermediate face");
        retained.undo();
        retained.undo();
        check(same(*retained.bodies().at(1), *retainedBefore),
              "Two retained-face steps undo exactly");
        auto retainedOpening = box();
        const auto openingFace = patch(retainedOpening);
        const auto openingBefore = retainedOpening.bodies().at(1);
        rejects([&] { retainedOpening.pushPull(1, openingFace, -2, true); });
        check(retainedOpening.bodies().at(1) == openingBefore,
              "Retaining a face at an opening rejects atomically");
        auto d = box();
        auto before = d.bodies().at(1);
        auto top = upward(before->surface);
        d.pushPull(1, top, 3);
        closed(d.bodies().at(1)->surface, 80);
        check(d.bodies().at(1)->surface.faces == before->surface.faces,
              "Prism cap retains all face IDs and loops");
        d.pushPull(1, top, -4);
        closed(d.bodies().at(1)->surface, 16);
        auto shortBox = d.bodies().at(1);
        rejects([&] { d.pushPull(1, top, -2); });
        check(d.bodies().at(1) == shortBox, "Crossing opposite face rejects atomically");
        rejects([&] { d.pushPull(1, top, -1); });
        check(d.bodies().at(1) == shortBox, "Collapsing complete prism rejects atomically");
        d.undo();
        d.undo();
        check(same(*d.bodies().at(1), *before), "Cap undo restores exact topology");
        d.redo();
        closed(d.bodies().at(1)->surface, 80);
        for (double distance : {2.0, -2.0}) {
            Document profile;
            auto b =
                profile.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {2, 1, 0}, {2, 4, 0}, {0, 4, 0}},
                                 {{.5, .5, 0}, {.5, 1.5, 0}, {1.5, 1.5, 0}, {1.5, .5, 0}}});
            auto face = profile.bodies().at(b)->surface.faces.begin()->first;
            auto area = profile.bodies().at(b)->surface.area(face);
            auto report = profile.pushPull(b, face, distance);
            closed(profile.bodies().at(b)->surface, area * 2);
            check(report.at(b).faces.descendants.at(face).size() == 2,
                  "Isolated face maps to base and cap");
            auto cap = upward(profile.bodies().at(b)->surface);
            profile.pushPull(b, cap, 1);
            closed(profile.bodies().at(b)->surface, area * 3);
        }
        auto outward = box();
        auto selected = patch(outward);
        outward.pushPull(1, selected, 1);
        closed(outward.bodies().at(1)->surface, 36);
        auto recess = box();
        selected = patch(recess);
        auto recessBefore = recess.bodies().at(1);
        recess.pushPull(1, selected, -1);
        closed(recess.bodies().at(1)->surface, 28);
        recess.undo();
        check(same(*recess.bodies().at(1), *recessBefore), "Recess undo restores selected region");
        auto wall = box();
        selected = patch(wall);
        auto wallBefore = wall.bodies().at(1);
        auto cut = wall.pushPull(1, selected, -2);
        auto opening = wall.bodies().at(1);
        closed(opening->surface, 24);
        check(opening->surface.faces.size() == 10,
              "Opening has outer walls, tunnel walls and two holed caps");
        check(cut.at(1).faces.descendants.at(selected).empty(), "Opening retires selected cap");
        wall.undo();
        check(same(*wall.bodies().at(1), *wallBefore), "Opening undo restores exact topology");
        wall.redo();
        check(same(*wall.bodies().at(1), *opening), "Opening redo restores exact identities");
        // A holed patch sweeps an annulus, leaving the center pillar intact.
        auto annulus = box();
        auto outer = patch(annulus);
        annulus.insertEdges(1, {0, 0, 2}, {0, 0, 1},
                            {{{{1.5, 1.5, 2}, {2.5, 1.5, 2}}},
                             {{{2.5, 1.5, 2}, {2.5, 2.5, 2}}},
                             {{{2.5, 2.5, 2}, {1.5, 2.5, 2}}},
                             {{{1.5, 2.5, 2}, {1.5, 1.5, 2}}}});
        for (const auto &[id, f] : annulus.bodies().at(1)->surface.faces)
            if (annulus.bodies().at(1)->surface.normal(id).z > .99 &&
                std::abs(annulus.bodies().at(1)->surface.area(id) - 3) < 1e-8)
                outer = id;
        annulus.pushPull(1, outer, -2);
        closed(annulus.bodies().at(1)->surface, 26);
        Document separate;
        auto part = std::make_shared<Body>();
        part->id = 1;
        auto isolated = part->surface.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        part->surface.addFace({{{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}}});
        separate.apply({"Two islands", {{1, nullptr, part}}}, separate.revision());
        separate.pushPull(1, isolated, 2);
        check(separate.bodies().at(1)->surface.faces.size() == 7,
              "Disconnected isolated face keeps its base");
        auto invalid = box();
        selected = patch(invalid);
        auto safe = invalid.bodies().at(1);
        auto revision = invalid.revision();
        rejects([&] { invalid.pushPull(1, selected, -3); });
        rejects([&] { invalid.pushPull(1, selected, NAN); });
        rejects([&] { invalid.pushPull(1, selected, 1e7); });
        check(invalid.bodies().at(1) == safe && invalid.revision() == revision,
              "Invalid sweeps leave document unchanged");
        auto obstacle = std::make_shared<Body>(*safe);
        obstacle->surface.addFace({{{1, 1, 1}, {3, 1, 1.5}, {3, 3, 1.5}, {1, 3, 1}}});
        invalid.apply({"Obstacle", {{1, safe, obstacle}}}, invalid.revision());
        safe = invalid.bodies().at(1);
        rejects([&] { invalid.pushPull(1, selected, -2); });
        check(invalid.bodies().at(1) == safe, "Sloped obstruction rejects atomically");
        std::cout << "Push/pull prism, concave/holed profiles, recesses, openings, collisions and "
                     "identity tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
