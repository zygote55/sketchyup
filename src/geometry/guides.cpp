#include "geometry/guides.hpp"
#include <numbers>
namespace sketchy {
namespace {
Vec3 unit(Vec3 value) {
    const auto n = length(value);
    if (!std::isfinite(n) || n == 0)
        throw std::runtime_error("Guide direction must be finite and nonzero");
    return value * (1 / n);
}
} // namespace
void Guide::validate() const {
    checkPoint(origin);
    if (kind == GuideKind::Point) {
        if (direction != Vec3{})
            throw std::runtime_error("Guide point must not carry a direction");
    } else if (kind == GuideKind::Line) {
        const auto n = length(direction);
        if (!std::isfinite(n) || std::abs(n - 1) > 1e-10)
            throw std::runtime_error("Guide line requires a unit direction");
    } else
        throw std::runtime_error("Unknown guide kind");
}
Guide guidePoint(Vec3 point) {
    Guide result{GuideKind::Point, point, {}};
    result.validate();
    return result;
}
Guide guideLine(Vec3 origin, Vec3 direction) {
    Guide result{GuideKind::Line, origin, unit(direction)};
    result.validate();
    return result;
}
std::array<Vec3, 2> boundedGuideLine(const Guide &line) {
    line.validate();
    if (line.kind != GuideKind::Line)
        throw std::runtime_error("Expected a guide line");
    double low = -INFINITY, high = INFINITY;
    const double origins[]{line.origin.x, line.origin.y, line.origin.z};
    const double directions[]{line.direction.x, line.direction.y, line.direction.z};
    for (int i = 0; i < 3; ++i) {
        if (directions[i] == 0)
            continue;
        auto a = (-coordinateLimit - origins[i]) / directions[i];
        auto b = (coordinateLimit - origins[i]) / directions[i];
        if (a > b)
            std::swap(a, b);
        low = std::max(low, a);
        high = std::min(high, b);
    }
    return {line.origin + line.direction * low, line.origin + line.direction * high};
}
Guide offsetGuide(const Guide &line, Vec3 normal, double distance) {
    line.validate();
    normal = unit(normal);
    if (line.kind != GuideKind::Line || std::abs(dot(line.direction, normal)) > 1e-10)
        throw std::runtime_error("Offset requires a line lying in the specified plane");
    if (!std::isfinite(distance) || std::abs(distance) <= tolerance)
        throw std::runtime_error("Guide offset must exceed modeling tolerance");
    const auto direction = unit(line.direction);
    return guideLine(line.origin + cross(normal, direction) * distance, direction);
}
Guide angledGuide(const DrawingPlane &plane, double angle) {
    if (!std::isfinite(angle) || std::abs(angle) > 2 * std::numbers::pi)
        throw std::runtime_error("Guide angle must be within one signed revolution");
    const auto frame = DrawingPlane::make(plane.origin, plane.normal, plane.xAxis);
    return guideLine(frame.origin, frame.xAxis * std::cos(angle) + frame.yAxis * std::sin(angle));
}
double measureDistance(Vec3 start, Vec3 end) {
    checkPoint(start);
    checkPoint(end);
    return length(end - start);
}
double measureAngle(Vec3 origin, Vec3 first, Vec3 second, Vec3 normal) {
    for (auto point : {origin, first, second})
        checkPoint(point);
    if (length(first - origin) <= tolerance || length(second - origin) <= tolerance)
        throw std::runtime_error("Angle requires two nonzero rays");
    const auto frame = DrawingPlane::make(origin, normal, first - origin);
    if (std::abs(dot(first - origin, frame.normal)) > tolerance ||
        std::abs(dot(second - origin, frame.normal)) > tolerance)
        throw std::runtime_error("Angle rays must lie in the specified plane");
    const auto other = unit(second - origin);
    return std::atan2(dot(other, frame.yAxis), dot(other, frame.xAxis));
}
void validateGuides(const std::map<Id, Guide> &guides, const Surface &surface) {
    if (guides.size() > 1024)
        throw std::runtime_error("Too many guides in one context");
    for (const auto &[id, guide] : guides) {
        if (!id || id >= surface.nextId || surface.vertices.contains(id) ||
            surface.faces.contains(id))
            throw std::runtime_error("Invalid guide identity");
        guide.validate();
    }
}
EntityChanges compareGuides(const std::map<Id, Guide> &before, const std::map<Id, Guide> &after) {
    EntityChanges result;
    for (const auto &[id, guide] : before) {
        if (!after.contains(id)) {
            result.deleted.push_back(id);
            result.descendants[id] = {};
        } else if (guide != after.at(id))
            result.modified.push_back(id);
    }
    for (const auto &[id, guide] : after)
        if (!before.contains(id))
            result.created.push_back(id);
    return result;
}
} // namespace sketchy
