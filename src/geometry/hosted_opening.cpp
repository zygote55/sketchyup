#include "geometry/hosted_opening.hpp"
#include "geometry/drawing.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
#include <limits>
namespace sketchy {
namespace {
using namespace Clipper2Lib;
constexpr size_t faceLimit = 1000, vertexLimit = 10000, cornerLimit = 32000, profileLimit = 256,
                 workLimit = 4000000;
constexpr double precision = 1e8, clearance = tolerance * 4;
[[noreturn]] void fail(const char *code, const std::string &message) {
    throw OpeningError(code, message);
}
void preflight(const Surface &host, size_t count) {
    if (count < 3 || count > profileLimit)
        fail("OPENING_PROFILE", "Opening requires a simple outline with 3 to 256 corners");
    if (host.faces.size() > faceLimit - count || host.vertices.size() > vertexLimit - 2 * count ||
        !host.wires.empty())
        fail("OPENING_LIMIT", "Opening requires a bounded closed host without wire geometry");
    size_t corners = 6 * count;
    for (const auto &[id, record] : host.faces) {
        if (record.loops.empty() || record.loops.size() > 64)
            fail("OPENING_LIMIT", "Host faces accept at most 64 boundary loops");
        for (const auto &loop : record.loops) {
            if (loop.size() > 4096 || loop.size() > cornerLimit - corners)
                fail("OPENING_LIMIT", "Opening exceeds the host boundary budget");
            corners += loop.size();
        }
    }
}
SolidShellAnalysis checkedSolid(const Surface &surface, const char *code) {
    surface.validate();
    auto result = analyzeSolidShells(surface, Topology::rebuild(surface, {}));
    if (result.report.status != "validated_shells")
        fail(code, "Opening requires validated closed geometry: " + result.report.status);
    for (const auto &shell : result.shells)
        if ((shell.signedVolume > 0) != (shell.depth % 2 == 0))
            fail(code, "Orient host material outward and cavity boundaries inward before cutting");
    return result;
}
struct Projection {
    DrawingPlane plane;
    Point64 point(Vec3 p) const {
        const auto local = plane.coordinates(p);
        return {std::llround(local.x * precision), std::llround(local.y * precision)};
    }
    Path64 outline(const Surface &s, const std::vector<Id> &loop) const {
        Path64 path;
        path.reserve(loop.size());
        for (auto id : loop)
            path.push_back(point(s.vertices.at(id)));
        return path;
    }
    Paths64 face(const Surface &s, Id id) const {
        Paths64 paths;
        for (const auto &loop : s.faces.at(id).loops) {
            auto path = outline(s, loop);
            if (IsPositive(path) != paths.empty())
                std::reverse(path.begin(), path.end());
            paths.push_back(std::move(path));
        }
        return paths;
    }
};
void charge(size_t &budget, size_t a, size_t b) {
    if (a && b > budget / a)
        fail("OPENING_LIMIT", "Opening intersection work budget exceeded");
    budget -= a * b;
}
bool contains(const Paths64 &face, const Paths64 &expanded, size_t &budget) {
    size_t a = 0, b = 0;
    for (const auto &path : face)
        a += path.size();
    for (const auto &path : expanded)
        b += path.size();
    charge(budget, a, b);
    return Difference(expanded, face, FillRule::NonZero).empty();
}
std::vector<Vec3> clip(const std::vector<Vec3> &polygon, const DrawingPlane &plane, double bound,
                       bool above) {
    std::vector<Vec3> result;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const auto da = dot(a - plane.origin, plane.normal) - bound,
                   db = dot(b - plane.origin, plane.normal) - bound;
        const bool ia = above ? da >= 0 : da <= 0, ib = above ? db >= 0 : db <= 0;
        if (ia)
            result.push_back(a);
        if (ia != ib)
            result.push_back(a + (b - a) * (da / (da - db)));
    }
    return result;
}
void clearSweep(const Surface &host, Id entry, Id exit, const Projection &projection,
                const Path64 &profile, double depth, size_t &budget) {
    for (const auto &[id, face] : host.faces) {
        if (id == entry || id == exit)
            continue;
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const auto &loop : face.loops)
            for (auto vertex : loop) {
                const auto distance = projection.plane.coordinates(host.vertices.at(vertex)).z;
                low = std::min(low, distance);
                high = std::max(high, distance);
            }
        if (low > tolerance || high < -depth - tolerance)
            continue;
        for (const auto &triangle : host.triangulate(id)) {
            auto polygon = clip({triangle.a, triangle.b, triangle.c}, projection.plane,
                                -depth - tolerance, true);
            polygon = clip(polygon, projection.plane, tolerance, false);
            if (polygon.empty())
                continue;
            charge(budget, polygon.size(), profile.size());
            Path64 path;
            for (auto point : polygon)
                path.push_back(projection.point(point));
            if (std::abs(Area(path)) >= 1) {
                if (!Intersect(Paths64{path}, Paths64{profile}, FillRule::NonZero).empty())
                    fail("OPENING_OBSTRUCTED", "Opening would cross an intervening host face");
            } else {
                // A face parallel to the sweep projects to a line. Positive
                // length inside the cut profile is still an obstruction.
                path.push_back(path.front());
                Clipper64 engine;
                engine.AddOpenSubject({path});
                engine.AddClip({profile});
                Paths64 closed, open;
                engine.Execute(ClipType::Intersection, FillRule::NonZero, closed, open);
                for (const auto &line : open)
                    for (size_t i = 1; i < line.size(); ++i)
                        if (line[i] != line[i - 1])
                            fail("OPENING_OBSTRUCTED", "Opening intersects an interior wall");
                for (const auto &point : path)
                    if (PointInPolygon(point, profile) != PointInPolygonResult::IsOutside)
                        fail("OPENING_OBSTRUCTED", "Opening touches intervening geometry");
            }
        }
    }
}
HostedOpening build(const Surface &host, Id entry, const std::vector<Vec3> &input) {
    preflight(host, input.size());
    if (!entry || !host.faces.contains(entry))
        fail("OPENING_HOST", "Choose an existing host face");
    const auto before = checkedSolid(host, "OPENING_HOST");
    const auto normal = host.normal(entry);
    const auto origin = host.vertices.at(host.faces.at(entry).loops.front().front());
    const Projection projection{DrawingPlane::make(
        origin, normal, std::abs(normal.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0})};
    std::vector<Vec3> profile;
    Path64 path;
    for (auto point : input) {
        checkPoint(point);
        const auto distance = projection.plane.coordinates(point).z;
        if (std::abs(distance) > tolerance)
            fail("OPENING_PROFILE", "Opening outline must lie on the selected host face");
        point = point - normal * distance;
        profile.push_back(point);
        path.push_back(projection.point(point));
    }
    Surface outline;
    Id outlineFace;
    try {
        outlineFace = outline.addFace({profile});
    } catch (const std::exception &error) {
        throw OpeningError("OPENING_PROFILE", error.what());
    }
    if (!IsPositive(path)) {
        std::reverse(profile.begin(), profile.end());
        std::reverse(path.begin(), path.end());
    }
    const auto expanded =
        InflatePaths(Paths64{path}, clearance * precision, JoinType::Miter, EndType::Polygon);
    size_t budget = workLimit;
    if (expanded.empty() || !contains(projection.face(host, entry), expanded, budget))
        fail("OPENING_PROFILE", "Opening needs clearance from host boundaries and existing holes");
    Id exit = 0;
    double depth = std::numeric_limits<double>::infinity();
    for (const auto &[id, face] : host.faces) {
        if (id == entry || dot(host.normal(id), normal) > -1 + 1e-10)
            continue;
        const auto distance =
            -projection.plane.coordinates(host.vertices.at(face.loops.front().front())).z;
        if (distance <= clearance || distance >= depth)
            continue;
        bool parallel = true;
        for (const auto &loop : face.loops)
            for (auto vertex : loop)
                parallel &= std::abs(projection.plane.coordinates(host.vertices.at(vertex)).z +
                                     distance) <= tolerance;
        if (parallel && contains(projection.face(host, id), expanded, budget)) {
            depth = distance;
            exit = id;
        }
    }
    if (!exit)
        fail("OPENING_EXIT", "No parallel opposing face covers the complete opening outline");
    if (host.faces.at(entry).loops.size() >= 64 || host.faces.at(exit).loops.size() >= 64)
        fail("OPENING_LIMIT", "Opening would exceed the host face loop limit");
    clearSweep(host, entry, exit, projection, path, depth, budget);
    HostedOpening result{
        {host, {}, {}, {}}, entry, exit, depth, outline.area(outlineFace) * depth, {}};
    auto &surface = result.edit.surface;
    std::vector<Id> front, back;
    for (auto point : profile) {
        front.push_back(surface.vertex(point));
        back.push_back(surface.vertex(point - normal * depth));
    }
    auto hole = front;
    std::reverse(hole.begin(), hole.end());
    surface.faces.at(entry).loops.push_back(std::move(hole));
    surface.faces.at(exit).loops.push_back(back);
    result.edit.faces[entry] = {entry};
    result.edit.faces[exit] = {exit};
    for (size_t i = 0; i < front.size(); ++i) {
        const auto j = (i + 1) % front.size();
        const auto jamb = surface.addFaceIds({{front[i], front[j], back[j], back[i]}});
        result.jambs.push_back(jamb);
        result.edit.faces[entry].push_back(jamb);
    }
    const auto after = checkedSolid(surface, "OPENING_RESULT");
    const auto removed = *before.report.volume - *after.report.volume;
    if (std::abs(removed - result.removedVolume) >
        std::max({1e-12, result.removedVolume * 1e-6, *before.report.volume * 1e-10}))
        fail("OPENING_RESULT", "Opening material volume does not match its bounded prism");
    return result;
}
} // namespace
HostedOpening cutHostedOpening(const Surface &host, Id face, const std::vector<Vec3> &profile) {
    try {
        return build(host, face, profile);
    } catch (const OpeningError &) {
        throw;
    } catch (const std::exception &error) {
        throw OpeningError("OPENING_INVALID", error.what());
    }
}
} // namespace sketchy
