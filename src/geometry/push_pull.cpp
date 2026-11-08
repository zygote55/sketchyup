#include "geometry/push_pull.hpp"
#include <algorithm>
#include <array>
#include <clipper2/clipper.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
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
// The area of the overlap of the two paths' bounding boxes, in units squared: an upper bound on
// the area of their intersection (R117, D-106).
double boundingOverlapArea(const Paths64 &a, const Paths64 &b) {
    auto box = [](const Paths64 &paths) {
        std::array<int64_t, 4> result{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
        for (const auto &path : paths)
            for (const auto &point : path) {
                result[0] = std::min(result[0], point.x);
                result[1] = std::min(result[1], point.y);
                result[2] = std::max(result[2], point.x);
                result[3] = std::max(result[3], point.y);
            }
        return result;
    };
    const auto p = box(a), q = box(b);
    const double width = double(std::min(p[2], q[2]) - std::max(p[0], q[0]));
    const double height = double(std::min(p[3], q[3]) - std::max(p[1], q[1]));
    return width > 0 && height > 0 ? width * height : 0;
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
// The coplanar check (R117, D-106). It throws for a pair of faces, the first in ID order, when the
// second lies within the tolerance of the first's plane and the two regions they project to overlap
// in an area of at least one unit: the pairwise check it replaces, with the same predicates. Such a
// pair has overlapping bounding boxes: the regions lie within their vertices' boxes, and each
// second face lies within the tolerance of the first's plane, so both regions reach a common point
// within a margin of both boxes. So faces are binned in a uniform grid of cells about the size of a
// typical face, and each overlapping pair is tested once, in the cell that holds the lower corner
// of its overlap. A face too large for the grid is tested against every face instead. A region lies
// in the intersection of the two bounding boxes, so a pair whose boxes overlap in less than one
// unit is not tested further. Candidate tests are bounded by a budget (fail-closed).
void checkCoplanarOverlap(const Surface &s) {
    constexpr double margin = 4 * tolerance;
    constexpr double infinity = std::numeric_limits<double>::infinity();
    struct Entry {
        Id id;
        Plane plane;
        Vec3 lo{infinity, infinity, infinity}, hi{-infinity, -infinity, -infinity};
        bool large{};
    };
    std::vector<Entry> entries;
    std::vector<double> extents;
    for (const auto &[id, record] : s.faces) {
        Entry entry{id, plane(s, id)};
        for (const auto &loop : record.loops)
            for (auto v : loop) {
                const auto p = s.vertices.at(v);
                entry.lo = {std::min(entry.lo.x, p.x), std::min(entry.lo.y, p.y),
                            std::min(entry.lo.z, p.z)};
                entry.hi = {std::max(entry.hi.x, p.x), std::max(entry.hi.y, p.y),
                            std::max(entry.hi.z, p.z)};
            }
        const auto size = entry.hi - entry.lo;
        extents.push_back(std::max({size.x, size.y, size.z}));
        entries.push_back(std::move(entry));
    }
    if (entries.size() < 2)
        return;
    // The cell is the three-quarter extent of the faces: large faces are few, and each small face
    // covers few cells.
    const auto upper = extents.begin() + std::ptrdiff_t(extents.size() * 3 / 4);
    std::nth_element(extents.begin(), upper, extents.end());
    const double cell = std::max(*upper, 16 * tolerance);
    auto index = [&](double value) { return static_cast<long long>(std::floor(value / cell)); };
    using Cell = std::array<long long, 3>;
    constexpr size_t cellsPerFace = 512;
    std::map<Cell, std::vector<size_t>> grid;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        const Cell low{index(e.lo.x - margin), index(e.lo.y - margin), index(e.lo.z - margin)};
        const Cell high{index(e.hi.x + margin), index(e.hi.y + margin), index(e.hi.z + margin)};
        const auto count = size_t(high[0] - low[0] + 1) * size_t(high[1] - low[1] + 1) *
                           size_t(high[2] - low[2] + 1);
        if (count > cellsPerFace) {
            entries[i].large = true;
            continue;
        }
        for (auto x = low[0]; x <= high[0]; ++x)
            for (auto y = low[1]; y <= high[1]; ++y)
                for (auto z = low[2]; z <= high[2]; ++z)
                    grid[{x, y, z}].push_back(i);
    }
    size_t budget = 64 * entries.size() + 1000000;
    auto overlaps = [&](const Entry &a, const Entry &b) {
        return a.lo.x - margin <= b.hi.x + margin && b.lo.x - margin <= a.hi.x + margin &&
               a.lo.y - margin <= b.hi.y + margin && b.lo.y - margin <= a.hi.y + margin &&
               a.lo.z - margin <= b.hi.z + margin && b.lo.z - margin <= a.hi.z + margin;
    };
    // i < j: the first face in ID order holds the plane, as the pairwise check did.
    auto testPair = [&](size_t i, size_t j) {
        if (!budget--)
            throw std::runtime_error("Push/pull face-pair budget exceeded");
        const auto &first = entries[i], &second = entries[j];
        for (const auto &loop : s.faces.at(second.id).loops)
            for (auto id : loop)
                if (std::abs(dot(s.vertices.at(id) - first.plane.origin, first.plane.n)) >
                    tolerance)
                    return;
        const auto a = paths(s, first.id, first.plane), b = paths(s, second.id, first.plane);
        if (boundingOverlapArea(a, b) < 1)
            return;
        if (std::abs(Area(Intersect(a, b, FillRule::NonZero))) >= 1)
            throw std::runtime_error("Push/pull would leave overlapping coplanar faces");
    };
    for (const auto &[cellKey, members] : grid)
        for (size_t p = 0; p < members.size(); ++p)
            for (size_t q = p + 1; q < members.size(); ++q) {
                const auto i = members[p], j = members[q];
                if (!overlaps(entries[i], entries[j]))
                    continue;
                // Test the pair only in the cell that holds the lower corner of its overlap.
                const Cell anchor{index(std::max(entries[i].lo.x, entries[j].lo.x) - margin),
                                  index(std::max(entries[i].lo.y, entries[j].lo.y) - margin),
                                  index(std::max(entries[i].lo.z, entries[j].lo.z) - margin)};
                if (anchor == cellKey)
                    testPair(i, j);
            }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].large)
            continue;
        for (size_t j = 0; j < entries.size(); ++j) {
            if (j == i || (entries[j].large && j < i) || !overlaps(entries[i], entries[j]))
                continue;
            testPair(std::min(i, j), std::max(i, j));
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
    // A cut segment lies on the destination plane, so a vertex it can pass through lies within the
    // tolerance of that plane. Only those vertices are tested (R117, D-106).
    std::vector<std::pair<Id, Vec3>> onPlane;
    for (const auto &[id, point] : s.vertices)
        if (std::abs(dot(point - p.origin, p.n)) <= 2 * tolerance)
            onPlane.emplace_back(id, point);
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
                        if (onPlane.size() > budget)
                            throw std::runtime_error("Opposite boundary match budget exceeded");
                        budget -= onPlane.size();
                        for (const auto &[id, point] : onPlane) {
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
