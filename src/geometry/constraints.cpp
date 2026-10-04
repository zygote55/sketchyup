#include "geometry/constraints.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
Vec3 unit(Vec3 direction) {
    const auto n = length(direction);
    if (!std::isfinite(n) || n == 0)
        throw std::runtime_error("Constraint direction must be finite and nonzero");
    return direction * (1 / n);
}
DirectionConstraint canonical(DirectionConstraint constraint) {
    checkPoint(constraint.origin);
    constraint.direction = unit(constraint.direction);
    return constraint;
}
bool coplanar(const DirectionConstraint &constraint, const DrawingPlane &plane) {
    return std::abs(dot(constraint.origin - plane.origin, plane.normal)) <= tolerance &&
           std::abs(dot(constraint.direction, plane.normal)) <= 1e-10;
}
} // namespace
const char *directionLabel(DirectionKind kind) {
    switch (kind) {
    case DirectionKind::RedAxis:
        return "On red axis";
    case DirectionKind::GreenAxis:
        return "On green axis";
    case DirectionKind::BlueAxis:
        return "On blue axis";
    case DirectionKind::Parallel:
        return "Parallel";
    case DirectionKind::Perpendicular:
        return "Perpendicular";
    case DirectionKind::Tangent:
        return "Tangent";
    case DirectionKind::FromPoint:
        return "From point";
    }
    return "";
}
std::optional<DirectionCandidate> projectDirection(const DirectionConstraint &value,
                                                   const InferenceCamera &camera, double x,
                                                   double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(camera.width) ||
        !std::isfinite(camera.height) || camera.width <= 0 || camera.height <= 0 ||
        std::any_of(camera.clipFromWorld.begin(), camera.clipFromWorld.end(),
                    [](double value) { return !std::isfinite(value); }))
        throw std::runtime_error("Invalid directional inference camera");
    const auto constraint = canonical(value);
    const auto &m = camera.clipFromWorld;
    auto clip = [&](Vec3 p, double w) {
        return std::array<double, 4>{m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * w,
                                     m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * w,
                                     m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * w,
                                     m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15] * w};
    };
    const auto a = clip(constraint.origin, 1), d = clip(constraint.direction, 0);
    const auto u0 = camera.width * .5 * (a[0] + a[3]), v0 = camera.height * .5 * (a[3] - a[1]);
    const auto u1 = camera.width * .5 * (d[0] + d[3]), v1 = camera.height * .5 * (d[3] - d[1]);
    const auto lx = v0 * d[3] - a[3] * v1, ly = a[3] * u1 - u0 * d[3], lz = u0 * v1 - v0 * u1;
    const auto denominator = lx * lx + ly * ly;
    if (!std::isfinite(denominator) || denominator < 1e-16)
        return {};
    const auto offset = (lx * x + ly * y + lz) / denominator;
    const auto sx = x - lx * offset, sy = y - ly * offset;
    const auto dx = u1 - sx * d[3], dy = v1 - sy * d[3];
    const auto t = std::abs(dx) >= std::abs(dy) ? (sx * a[3] - u0) / dx : (sy * a[3] - v0) / dy;
    const auto point = constraint.origin + constraint.direction * t;
    if (!std::isfinite(t) || std::abs(point.x) > coordinateLimit ||
        std::abs(point.y) > coordinateLimit || std::abs(point.z) > coordinateLimit)
        return {};
    if (!camera.project(point))
        return {};
    return DirectionCandidate{constraint, point, std::hypot(sx - x, sy - y)};
}
std::vector<DirectionCandidate>
directionCandidates(const InferenceCamera &camera, double x, double y, Vec3 anchor,
                    const DrawingPlane &plane, const std::vector<DirectionConstraint> &references,
                    std::optional<Vec3> fromPoint, double radius) {
    if (!std::isfinite(radius) || radius <= 0 || radius > 64)
        throw std::runtime_error("Invalid directional acquisition radius");
    const auto frame = DrawingPlane::make(plane.origin, plane.normal, plane.xAxis);
    std::vector<DirectionConstraint> constraints{{DirectionKind::RedAxis, anchor, {1, 0, 0}},
                                                 {DirectionKind::GreenAxis, anchor, {0, 1, 0}},
                                                 {DirectionKind::BlueAxis, anchor, {0, 0, 1}}};
    for (auto reference : references) {
        reference.origin = anchor;
        constraints.push_back(reference);
    }
    if (fromPoint)
        for (auto axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}})
            constraints.push_back({DirectionKind::FromPoint, *fromPoint, axis});
    std::vector<DirectionCandidate> result;
    for (auto constraint : constraints) {
        constraint = canonical(constraint);
        if (!coplanar(constraint, frame))
            continue;
        if (const auto candidate = projectDirection(constraint, camera, x, y);
            candidate && candidate->pixels <= radius)
            result.push_back(*candidate);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        // Opposite representations of the same projected line can differ by
        // roundoff. Tie within a millionth of a logical pixel, never in world
        // geometry, so canonical axis priority is stable across camera/scale.
        return std::tuple{
                   std::llround(a.pixels * 1e6), a.constraint.kind,        a.constraint.body,
                   a.constraint.entity,          a.constraint.direction.x, a.constraint.direction.y,
                   a.constraint.direction.z} <
               std::tuple{
                   std::llround(b.pixels * 1e6), b.constraint.kind,        b.constraint.body,
                   b.constraint.entity,          b.constraint.direction.x, b.constraint.direction.y,
                   b.constraint.direction.z};
    });
    return result;
}
Vec3 constrainedLength(const DirectionConstraint &value, Vec3 anchor, Vec3 preview,
                       double distance) {
    if (!std::isfinite(distance) || distance <= tolerance)
        throw std::runtime_error("Length must exceed modeling tolerance");
    const auto constraint = canonical(value);
    const auto offset = constraint.origin - anchor;
    const auto b = dot(offset, constraint.direction);
    const auto discriminant = distance * distance - dot(offset, offset) + b * b;
    if (discriminant < 0)
        throw std::runtime_error("Length cannot reach the locked reference line");
    const auto root = std::sqrt(discriminant),
               near = dot(preview - constraint.origin, constraint.direction);
    const auto first = -b + root, second = -b - root;
    const auto point =
        constraint.origin +
        constraint.direction * (std::abs(first - near) <= std::abs(second - near) ? first : second);
    checkPoint(point);
    return point;
}
std::vector<DirectionConstraint> edgeDirections(const Document &doc,
                                                const InferenceCandidate &reference, Vec3 anchor,
                                                const DrawingPlane &plane) {
    if (!doc.bodies().contains(reference.body))
        return {};
    const auto &body = *doc.bodies().at(reference.body);
    const auto world = doc.worldTransform(reference.body);
    std::vector<Id> edges;
    if (reference.entityType == InferenceEntity::Edge &&
        body.topology.edges.contains(reference.entity))
        edges.push_back(reference.entity);
    else if (reference.entityType == InferenceEntity::Vertex)
        for (const auto &[id, edge] : body.topology.edges)
            if (edge.a == reference.entity || edge.b == reference.entity)
                if (edges.size() < 64)
                    edges.push_back(id);
    std::set<Id> processedCurves;
    std::vector<DirectionConstraint> result;
    for (auto id : edges) {
        const auto &edge = body.topology.edges.at(id);
        const auto direction =
            unit(world.vector(body.surface.vertices.at(edge.b) - body.surface.vertices.at(edge.a)));
        result.push_back({DirectionKind::Parallel, anchor, direction, body.id, id});
        const auto perpendicular = cross(plane.normal, direction);
        if (length(perpendicular) > 1e-10)
            result.push_back(
                {DirectionKind::Perpendicular, anchor, unit(perpendicular), body.id, id});
        for (const auto &[curveId, curve] : body.curves) {
            if (processedCurves.contains(curveId) ||
                std::none_of(curve.edges.begin(), curve.edges.end(),
                             [&](auto association) { return association.edge == id; }))
                continue;
            processedCurves.insert(curveId);
            const auto local = world.inverse().point(anchor) - curve.center;
            const auto xx = dot(curve.xAxis, curve.xAxis), xy = dot(curve.xAxis, curve.yAxis),
                       yy = dot(curve.yAxis, curve.yAxis), denominator = xx * yy - xy * xy;
            const auto u =
                (dot(local, curve.xAxis) * yy - dot(local, curve.yAxis) * xy) / denominator;
            const auto v =
                (dot(local, curve.yAxis) * xx - dot(local, curve.xAxis) * xy) / denominator;
            if (length(world.vector(local - curve.xAxis * u - curve.yAxis * v)) > tolerance)
                continue;
            auto inRange = [&](double angle) {
                if (curve.kind == CurveKind::Circle)
                    return true;
                const auto tau = 2 * std::acos(-1);
                auto delta = std::fmod(
                    (angle - curve.startAngle) * (curve.sweepAngle < 0 ? -1 : 1) + 2 * tau, tau);
                return delta <= std::abs(curve.sweepAngle) + 1e-10;
            };
            const auto squared = u * u + v * v, r = curve.radius;
            if (length(world.point(curve.point(std::atan2(v, u))) - anchor) <= tolerance) {
                const auto angle = std::atan2(v, u);
                if (inRange(angle))
                    result.push_back({DirectionKind::Tangent, anchor,
                                      unit(world.vector(curve.tangent(angle))), body.id, curveId});
            } else if (squared > r * r) {
                const auto foot = r * r / squared,
                           offset = r * std::sqrt(squared - r * r) / squared;
                for (auto sign : {-1, 1}) {
                    const auto x = foot * u - sign * offset * v, y = foot * v + sign * offset * u;
                    const auto angle = std::atan2(y, x);
                    if (!inRange(angle))
                        continue;
                    const auto contact = world.point(curve.point(angle));
                    result.push_back(
                        {DirectionKind::Tangent, anchor, unit(contact - anchor), body.id, curveId});
                }
            }
        }
    }
    std::vector<DirectionConstraint> unique;
    for (const auto &constraint : result)
        if (std::none_of(unique.begin(), unique.end(), [&](const auto &other) {
                return other.kind == constraint.kind && other.body == constraint.body &&
                       other.entity == constraint.entity &&
                       std::abs(dot(other.direction, constraint.direction)) > 1 - 1e-10;
            }))
            unique.push_back(constraint);
    return unique;
}
void DirectionLocks::toggle(DirectionConstraint constraint) {
    constraint = canonical(constraint);
    held_.reset();
    if (persistent_ && persistent_->kind == constraint.kind &&
        persistent_->body == constraint.body && persistent_->entity == constraint.entity &&
        std::abs(dot(persistent_->direction, constraint.direction)) > 1 - 1e-10)
        persistent_.reset();
    else
        persistent_ = constraint;
}
void DirectionLocks::hold(DirectionConstraint constraint) { held_ = canonical(constraint); }
} // namespace sketchy
