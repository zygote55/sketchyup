#include "geometry/sweep.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const std::string &message) {
    throw SweepError(code, message);
}
Vec3 transport(Vec3 from, Vec3 to, Vec3 value) {
    const auto c = std::clamp(dot(from, to), -1.0, 1.0);
    if (c < -1 + 1e-10)
        fail("SWEEP_SHARP_TURN", "The path reverses direction; split it at the reversal");
    const auto k = cross(from, to);
    return value + cross(k, value) + cross(k, cross(k, value)) * (1 / (1 + c));
}
SweepResult build(const Surface &source, Id faceId, std::vector<Vec3> path, bool closed) {
    if (path.size() > 129)
        fail("SWEEP_LIMIT", "Sweep accepts at most 128 path stations");
    if (closed && path.size() > 1 && path.front() == path.back())
        path.pop_back();
    if (path.size() < (closed ? 3u : 2u) || path.size() > 128)
        fail("SWEEP_INVALID_PATH",
             "Use 2–128 stations for an open path or 3–128 for a closed path");
    for (auto point : path) {
        try {
            checkPoint(point);
        } catch (const std::exception &) {
            fail("SWEEP_INVALID_PATH",
                 "Path coordinates must be finite and within the model range");
        }
    }
    const auto found = source.faces.find(faceId);
    if (!faceId || found == source.faces.end() || found->second.id != faceId)
        fail("SWEEP_INVALID_PROFILE", "Select an existing profile face");
    auto loops = found->second.loops;
    size_t vertices{};
    for (const auto &loop : loops)
        vertices += loop.size();
    if (loops.empty() || loops.size() > 32 || vertices > 256)
        fail("SWEEP_LIMIT", "Sweep accepts at most 256 profile vertices and 32 loops");
    try {
        (void)source.triangulate(faceId);
    } catch (const std::exception &) {
        fail("SWEEP_INVALID_PROFILE",
             "The profile must be a valid planar face with strictly contained holes");
    }
    if (closed && loops.size() > 1)
        fail("SWEEP_CLOSED_HOLES", "Closed paths with holed profiles need multiple-shell "
                                   "validation; use an open path or a solid profile");
    const size_t count = closed ? path.size() : path.size() - 1;
    if (vertices * count + (closed ? 0 : 2) > 4096)
        fail("SWEEP_LIMIT", "Sweep exceeds 4096 output faces");
    const auto normal = source.normal(faceId);
    const auto anchor = path.front();
    if (std::abs(dot(anchor - source.vertices.at(loops[0][0]), normal)) > tolerance)
        fail("SWEEP_PROFILE_ALIGNMENT", "The path must start in the selected profile plane");
    const auto profileU =
        normalized(source.vertices.at(loops[0][1]) - source.vertices.at(loops[0][0]));
    const auto profileV = cross(normal, profileU);
    std::vector<Vec3> tangent;
    for (size_t i = 0; i < count; ++i) {
        const auto delta = path[(i + 1) % path.size()] - path[i];
        if (length(delta) < tolerance)
            fail("SWEEP_INVALID_PATH", "Consecutive path stations must be distinct");
        tangent.push_back(normalized(delta));
    }
    std::vector<Vec3> u(count), v(count);
    // A 180-degree initial alignment has a deterministic rotation around profile U.
    u[0] =
        dot(normal, tangent[0]) < -1 + 1e-10 ? profileU : transport(normal, tangent[0], profileU);
    u[0] = normalized(u[0] - tangent[0] * dot(u[0], tangent[0]));
    v[0] = cross(tangent[0], u[0]);
    for (size_t i = 1; i < count; ++i) {
        u[i] = transport(tangent[i - 1], tangent[i], u[i - 1]);
        u[i] = normalized(u[i] - tangent[i] * dot(u[i], tangent[i]));
        v[i] = cross(tangent[i], u[i]);
    }
    double radius{};
    for (size_t h = 0; h < loops.size(); ++h) {
        Vec3 area{};
        const auto origin = source.vertices.at(loops[h][0]);
        for (size_t j = 0; j < loops[h].size(); ++j) {
            const auto a = source.vertices.at(loops[h][j]);
            area = area + cross(a - origin,
                                source.vertices.at(loops[h][(j + 1) % loops[h].size()]) - origin);
            radius = std::max(radius, length(a - anchor));
        }
        if ((dot(area, normal) > 0) != (h == 0))
            std::reverse(loops[h].begin(), loops[h].end());
    }
    if (closed) {
        const auto seam = transport(tangent.back(), tangent.front(), u.back());
        const auto error = length(seam - u.front());
        if (error > 1e-8 || error * radius > tolerance * .25)
            fail("SWEEP_TWIST", "The closed path returns a twisted profile frame; split the path "
                                "or use a planar loop");
    }
    SweepResult result;
    using Ring = std::vector<std::vector<Id>>;
    std::vector<Ring> rings;
    for (size_t station = 0; station < path.size(); ++station) {
        const auto segment = std::min(station, count - 1);
        const auto direction = tangent[segment];
        Vec3 miter = direction;
        double denominator = 1;
        if (closed || (station > 0 && station + 1 < path.size())) {
            const auto previous = (station + count - 1) % count;
            const auto sum = tangent[previous] + direction;
            if (length(sum) < 1e-6)
                fail("SWEEP_SHARP_TURN", "The path reverses direction at a corner");
            miter = normalized(sum);
            denominator = dot(direction, miter);
            if (denominator < .25)
                fail("SWEEP_SHARP_TURN",
                     "The corner exceeds the 4x miter limit; soften the path turn");
        }
        Ring ring;
        for (const auto &loop : loops) {
            ring.emplace_back();
            for (auto vertex : loop) {
                const auto relative = source.vertices.at(vertex) - anchor;
                const auto radial =
                    u[segment] * dot(relative, profileU) + v[segment] * dot(relative, profileV);
                const auto point =
                    path[station] + radial - direction * (dot(radial, miter) / denominator);
                try {
                    checkPoint(point);
                } catch (const std::exception &) {
                    fail("SWEEP_RANGE", "The swept profile exceeds the model coordinate range");
                }
                ring.back().push_back(result.surface.vertex(point));
            }
        }
        rings.push_back(std::move(ring));
    }
    result.segments.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const auto &a = rings[i], &b = rings[(i + 1) % rings.size()];
        // Validate every rail before constructing quads: a consumed next rail
        // could otherwise make the current side a bow-tie first.
        for (size_t h = 0; h < loops.size(); ++h)
            for (size_t j = 0; j < loops[h].size(); ++j) {
                if (dot(result.surface.vertices.at(b[h][j]) - result.surface.vertices.at(a[h][j]),
                        tangent[i]) <= tolerance)
                    fail("SWEEP_SELF_INTERSECTION",
                         "The profile consumes a path segment at its corners; reduce the profile "
                         "or lengthen the path");
            }
        for (size_t h = 0; h < loops.size(); ++h)
            for (size_t j = 0; j < loops[h].size(); ++j) {
                const auto next = (j + 1) % loops[h].size();
                Id face{};
                try {
                    face = result.surface.addFaceIds({{a[h][j], a[h][next], b[h][next], b[h][j]}});
                } catch (const std::exception &) {
                    fail("SWEEP_DEGENERATE",
                         "A swept side is degenerate or nonplanar within modeling tolerance");
                }
                result.segments[i].push_back(face);
                result
                    .sides[{std::min(loops[h][j], loops[h][next]),
                            std::max(loops[h][j], loops[h][next])}]
                    .push_back(face);
            }
    }
    if (!closed) {
        auto base = rings.front();
        for (auto &loop : base)
            std::reverse(loop.begin(), loop.end());
        result.caps.push_back(result.surface.addFaceIds(std::move(base)));
        result.caps.push_back(result.surface.addFaceIds(rings.back()));
    }
    result.surface.validate();
    const auto topology = Topology::rebuild(result.surface, {});
    const auto solid = inspectSolid(result.surface, topology);
    if (solid.status == "analysis_limit")
        fail("SWEEP_LIMIT", "Sweep solid verification exceeds its analysis budget");
    if (solid.status == "self_intersection")
        fail("SWEEP_SELF_INTERSECTION",
             "The swept profile intersects itself; reduce the profile or separate the path");
    if (solid.status != "solid" || !solid.volume)
        fail("SWEEP_INVALID_SOLID", "Sweep did not produce a valid closed solid: " + solid.status);
    result.volume = *solid.volume;
    return result;
}
} // namespace
SweepResult sweepProfile(const Surface &source, Id face, const std::vector<Vec3> &path,
                         bool closed) {
    try {
        return build(source, face, path, closed);
    } catch (const SweepError &) {
        throw;
    } catch (const std::exception &) {
        fail("SWEEP_DEGENERATE",
             "The swept profile cannot form valid native geometry at this scale");
    }
}
} // namespace sketchy
