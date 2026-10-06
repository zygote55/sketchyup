#include "core/component_placement.hpp"
#include <algorithm>
#include <clipper2/clipper.h>
namespace sketchy {
namespace {
using namespace Clipper2Lib;
constexpr size_t cornerLimit = 4096, loopLimit = 64;
constexpr double precision = 1e8;
[[noreturn]] void fail(const char *code, const char *message) {
    throw PlacementError(code, message);
}
Transform matrix(const DrawingPlane &frame) {
    Transform result;
    result.m = {frame.xAxis.x,  frame.xAxis.y,  frame.xAxis.z,  0,
                frame.yAxis.x,  frame.yAxis.y,  frame.yAxis.z,  0,
                frame.normal.x, frame.normal.y, frame.normal.z, 0,
                frame.origin.x, frame.origin.y, frame.origin.z, 1};
    result.validate();
    return result;
}
DrawingPlane glueFrame(const DrawingPlane &frame) {
    try {
        const auto canonical = DrawingPlane::make(frame.origin, frame.normal, frame.xAxis);
        if (length(frame.normal - canonical.normal) > 1e-10 ||
            length(frame.xAxis - canonical.xAxis) > 1e-10 ||
            length(frame.yAxis - canonical.yAxis) > 1e-10 || !std::isfinite(frame.yAxis.x) ||
            !std::isfinite(frame.yAxis.y) || !std::isfinite(frame.yAxis.z))
            fail("INVALID_GLUE_FRAME",
                 "Component glue axes must be unit, orthogonal and right-handed");
        return canonical;
    } catch (const PlacementError &) {
        throw;
    } catch (const std::exception &error) {
        throw PlacementError("INVALID_GLUE_FRAME", error.what());
    }
}
FacePlacement build(const Surface &host, Id face, const Transform &hostToWorld,
                    const DrawingPlane &componentGlueFrame, const FacePlacementOptions &options) {
    const auto found = host.faces.find(face);
    if (!face || found == host.faces.end() || found->second.id != face)
        fail("INVALID_HOST_FACE", "Select an existing host face");
    const auto &record = found->second;
    if (record.loops.empty() || record.loops.size() > loopLimit)
        fail("PLACEMENT_LIMIT", "Placement accepts at most 64 face boundary loops");
    size_t corners = 0;
    for (const auto &loop : record.loops) {
        if (loop.size() < 3)
            fail("INVALID_HOST_FACE", "Host face requires closed boundary loops");
        if (loop.size() > cornerLimit - corners)
            fail("PLACEMENT_LIMIT", "Placement accepts at most 4096 face corners");
        corners += loop.size();
        for (auto vertex : loop) {
            if (!vertex || !host.vertices.contains(vertex))
                fail("INVALID_HOST_FACE", "Host face references a missing vertex");
            checkPoint(host.vertices.at(vertex));
        }
    }
    // Validate just this bounded face, including crossing/touching boundaries,
    // before treating its loops as a containment region.
    try {
        (void)host.triangulate(face);
    } catch (const std::exception &error) {
        throw PlacementError("INVALID_HOST_FACE", error.what());
    }
    const auto source = glueFrame(componentGlueFrame);
    checkPoint(options.anchor);
    hostToWorld.validate();
    const auto normal = host.normal(face);
    const auto origin = host.vertices.at(record.loops.front().front());
    const auto hostLocal = DrawingPlane::make(origin, normal, options.tangent);
    const auto point = hostLocal.coordinates(options.anchor);
    if (std::abs(point.z) > tolerance)
        fail("ANCHOR_OFF_PLANE", "Placement anchor is not on the host plane");
    const auto anchor = options.anchor - normal * point.z;
    auto project = [&](Vec3 p) {
        const auto local = hostLocal.coordinates(p);
        return Point64(std::llround(local.x * precision), std::llround(local.y * precision));
    };
    const auto query = project(anchor);
    bool boundary = false;
    for (size_t i = 0; i < record.loops.size(); ++i) {
        Path64 path;
        path.reserve(record.loops[i].size());
        for (auto vertex : record.loops[i])
            path.push_back(project(host.vertices.at(vertex)));
        const auto location = PointInPolygon(query, path);
        if ((i == 0 && location == PointInPolygonResult::IsOutside) ||
            (i > 0 && location == PointInPolygonResult::IsInside))
            fail("ANCHOR_OUTSIDE_FACE",
                 "Placement anchor lies outside host material or inside a hole");
        boundary |= location == PointInPolygonResult::IsOn;
    }
    const auto inverse = hostToWorld.inverse();
    const Vec3 physicalNormal{
        inverse.m[0] * normal.x + inverse.m[1] * normal.y + inverse.m[2] * normal.z,
        inverse.m[4] * normal.x + inverse.m[5] * normal.y + inverse.m[6] * normal.z,
        inverse.m[8] * normal.x + inverse.m[9] * normal.y + inverse.m[10] * normal.z};
    // Transform the projected in-plane tangent, rather than a caller's normal
    // component: under a shear that normal component is no longer perpendicular.
    const auto target = DrawingPlane::make(hostToWorld.point(anchor), physicalNormal,
                                           hostToWorld.vector(hostLocal.xAxis));
    const auto world = matrix(target) * Transform::rotation({0, 0, 1}, options.rotationRadians) *
                       Transform::scaling(options.scale) * matrix(source).inverse();
    return {world, target, anchor, boundary};
}
} // namespace
FacePlacement componentPlacementOnFace(const Surface &host, Id face, const Transform &hostToWorld,
                                       const DrawingPlane &componentGlueFrame,
                                       const FacePlacementOptions &options) {
    try {
        return build(host, face, hostToWorld, componentGlueFrame, options);
    } catch (const PlacementError &) {
        throw;
    } catch (const std::exception &error) {
        throw PlacementError("INVALID_PLACEMENT", error.what());
    }
}
} // namespace sketchy
