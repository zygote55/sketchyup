#include "geometry/curves.hpp"
#include <algorithm>
#include <numbers>
#include <set>
namespace sketchy {
namespace {
constexpr double tau = 2 * std::numbers::pi;
Vec3 unit(Vec3 p) {
    const auto n = length(p);
    if (!std::isfinite(n) || n == 0)
        throw std::runtime_error("Invalid curve direction");
    return p * (1 / n);
}
} // namespace
Vec3 Curve::point(double angle) const {
    return center + xAxis * (radius * std::cos(angle)) + yAxis * (radius * std::sin(angle));
}
Vec3 Curve::tangent(double angle) const {
    return unit(xAxis * -std::sin(angle) + yAxis * std::cos(angle)) * (sweepAngle < 0 ? -1 : 1);
}
std::vector<std::array<Vec3, 2>> Curve::chords() const {
    checkPoint(center);
    if ((kind != CurveKind::Circle && kind != CurveKind::Arc && kind != CurveKind::Pie) ||
        !std::isfinite(radius) || radius <= tolerance || !std::isfinite(startAngle) ||
        std::abs(startAngle) > tau || !std::isfinite(sweepAngle) || sweepAngle == 0 ||
        std::abs(sweepAngle) > tau || segments < 1 || segments > 256 ||
        (kind == CurveKind::Circle && (segments < 3 || sweepAngle != tau)) ||
        (kind != CurveKind::Circle && std::abs(sweepAngle) >= tau) ||
        (kind == CurveKind::Pie && segments < 2))
        throw std::runtime_error("Invalid curve radius, angle or segment count");
    for (auto axis : {xAxis, yAxis})
        if (!std::isfinite(length(axis)) || length(axis) < 1e-12 || length(axis) > 1e12)
            throw std::runtime_error("Invalid curve parameter frame");
    if (length(cross(unit(xAxis), unit(yAxis))) < 1e-10)
        throw std::runtime_error("Singular curve parameter frame");
    std::vector<std::array<Vec3, 2>> result;
    auto a = point(startAngle);
    for (unsigned i = 1; i <= segments; ++i) {
        const auto b = kind == CurveKind::Circle && i == segments
                           ? point(startAngle)
                           : point(startAngle + sweepAngle * i / segments);
        result.push_back({a, b});
        a = b;
    }
    if (kind == CurveKind::Pie) {
        result.push_back({a, center});
        result.push_back({center, point(startAngle)});
    }
    for (const auto &edge : result) {
        checkPoint(edge[0]);
        checkPoint(edge[1]);
        if (length(edge[1] - edge[0]) < tolerance)
            throw std::runtime_error("Curve segment is below the modeling tolerance");
    }
    return result;
}
Curve centerCurve(CurveKind kind, const DrawingPlane &plane, double radius, double start,
                  double sweep, unsigned segments) {
    const auto p = DrawingPlane::make(plane.origin, plane.normal, plane.xAxis);
    Curve c{kind, p.origin, p.xAxis, p.yAxis, radius, start, sweep, segments, {}};
    c.chords();
    return c;
}
Curve twoPointArc(Vec3 start, Vec3 end, Vec3 normal, double bulge, unsigned segments) {
    checkPoint(start);
    checkPoint(end);
    const auto chord = end - start;
    const auto span = length(chord);
    if (span <= tolerance || !std::isfinite(bulge) || std::abs(bulge) <= tolerance)
        throw std::runtime_error("Arc endpoints and bulge must be distinct");
    const auto plane = DrawingPlane::make(start, normal, chord);
    if (std::abs(dot(chord, plane.normal)) > tolerance)
        throw std::runtime_error("Arc endpoints are outside its plane");
    const auto center = start + chord * .5 + plane.yAxis * (bulge / 2 - span * span / (8 * bulge));
    const auto radius = span * span / (8 * std::abs(bulge)) + std::abs(bulge) / 2;
    const auto frame = DrawingPlane::make(center, plane.normal, start - center);
    return centerCurve(CurveKind::Arc, frame, radius, 0, -4 * std::atan(2 * bulge / span),
                       segments);
}
Curve threePointArc(Vec3 start, Vec3 through, Vec3 end, unsigned segments) {
    for (auto p : {start, through, end})
        checkPoint(p);
    const auto ab = through - start, ac = end - start;
    const auto n = cross(ab, ac);
    if (length(ab) <= tolerance || length(ac) <= tolerance || length(through - end) <= tolerance ||
        length(cross(unit(ab), unit(ac))) < 1e-8)
        throw std::runtime_error("Three-point arc requires distinct non-collinear points");
    const auto center =
        start + (cross(n, ab) * dot(ac, ac) + cross(ac, n) * dot(ab, ab)) * (1 / (2 * dot(n, n)));
    const auto radius = length(start - center);
    const auto frame = DrawingPlane::make(center, n, start - center);
    const auto endVector = end - center;
    auto sweep = std::atan2(dot(endVector, frame.yAxis), dot(endVector, frame.xAxis));
    if (sweep <= 0)
        sweep += tau;
    return centerCurve(CurveKind::Arc, frame, radius, 0, sweep, segments);
}
bool bindCurve(Curve &curve, const Surface &surface, const Topology &topology, size_t &budget) {
    std::vector<OrientedEdge> bound;
    std::set<Id> used;
    for (const auto &chord : curve.chords()) {
        const auto delta = chord[1] - chord[0];
        const auto span = length(delta), slack = tolerance / span;
        const auto direction = delta * (1 / span);
        struct Piece {
            double a, b;
            Id id;
            bool reversed;
        };
        std::vector<Piece> pieces;
        for (const auto &[id, edge] : topology.edges) {
            if (budget == 0)
                throw std::runtime_error("Curve association work exceeds limit");
            --budget;
            const auto a = surface.vertices.at(edge.a) - chord[0];
            const auto b = surface.vertices.at(edge.b) - chord[0];
            auto ta = dot(a, direction) / span, tb = dot(b, direction) / span;
            if (length(a - direction * (ta * span)) > tolerance ||
                length(b - direction * (tb * span)) > tolerance)
                continue;
            const bool reversed = ta > tb;
            if (reversed)
                std::swap(ta, tb);
            if (ta < -slack || tb > 1 + slack || tb - ta < slack * .5)
                continue;
            pieces.push_back({ta, tb, id, reversed});
        }
        std::sort(pieces.begin(), pieces.end(),
                  [](const auto &a, const auto &b) { return a.a < b.a; });
        double covered = 0;
        for (const auto &p : pieces) {
            if (std::abs(p.a - covered) > slack || !used.insert(p.id).second)
                return false;
            covered = p.b;
            bound.push_back({p.id, p.reversed});
        }
        if (pieces.empty() || std::abs(covered - 1) > slack)
            return false;
    }
    curve.edges = std::move(bound);
    return true;
}
void validateCurves(const std::map<Id, Curve> &curves, const Surface &surface,
                    const Topology &topology) {
    if (curves.size() > 1024)
        throw std::runtime_error("Too many curve records");
    size_t budget = 1000000;
    for (const auto &[id, curve] : curves) {
        if (!id || id >= surface.nextId || surface.vertices.contains(id) ||
            surface.faces.contains(id))
            throw std::runtime_error("Invalid curve identity");
        auto expected = curve;
        if (!bindCurve(expected, surface, topology, budget) || expected.edges != curve.edges)
            throw std::runtime_error("Curve record disagrees with editable edges");
    }
}
EntityChanges compareCurves(const std::map<Id, Curve> &before, const std::map<Id, Curve> &after) {
    EntityChanges result;
    for (const auto &[id, curve] : before) {
        if (!after.contains(id)) {
            result.deleted.push_back(id);
            result.descendants[id] = {};
        } else if (after.at(id) != curve)
            result.modified.push_back(id);
    }
    for (const auto &[id, curve] : after)
        if (!before.contains(id))
            result.created.push_back(id);
    return result;
}
} // namespace sketchy
