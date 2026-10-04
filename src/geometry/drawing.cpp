#include "geometry/drawing.hpp"
#include <algorithm>
#include <numbers>
namespace sketchy {
DrawingPlane DrawingPlane::make(Vec3 origin, Vec3 normal, Vec3 xAxis) {
    checkPoint(origin);
    // Plane directions have no length units. Scale before normalizing so a
    // transformed face's area vector works at both small and large scene scales.
    auto direction = [](Vec3 value) {
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            throw std::runtime_error("Drawing plane directions must be finite");
        const auto scale = std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
        if (scale == 0)
            throw std::runtime_error("Drawing plane direction is zero");
        const auto unit = Vec3{value.x / scale, value.y / scale, value.z / scale};
        return unit * (1 / length(unit));
    };
    const auto n = direction(normal), axis = direction(xAxis);
    const auto projected = axis - n * dot(axis, n);
    if (length(projected) < 1e-10)
        throw std::runtime_error("Drawing plane directions are parallel");
    const auto u = direction(projected);
    return {origin, n, u, cross(n, u)};
}
std::vector<Vec3> rectangleOutline(const DrawingPlane &plane, double width, double height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= tolerance ||
        height <= tolerance)
        throw std::runtime_error("Rectangle dimensions must exceed the modeling tolerance");
    const auto p = DrawingPlane::make(plane.origin, plane.normal, plane.xAxis);
    std::vector<Vec3> result{p.point(0, 0), p.point(width, 0), p.point(width, height),
                             p.point(0, height)};
    for (auto point : result)
        checkPoint(point);
    return result;
}
std::vector<Vec3> polygonOutline(const DrawingPlane &plane, double radius, unsigned sides) {
    if (!std::isfinite(radius) || radius <= tolerance || sides < 3 || sides > 256)
        throw std::runtime_error("Polygon requires a positive radius and 3–256 sides");
    if (2 * radius * std::sin(std::numbers::pi / sides) < tolerance)
        throw std::runtime_error("Polygon segments are below the modeling tolerance");
    const auto p = DrawingPlane::make(plane.origin, plane.normal, plane.xAxis);
    std::vector<Vec3> result;
    result.reserve(sides);
    for (unsigned i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        const auto point = p.point(radius * std::cos(angle), radius * std::sin(angle));
        checkPoint(point);
        result.push_back(point);
    }
    return result;
}
std::vector<std::array<Vec3, 2>> polylineEdges(const std::vector<Vec3> &points, bool closed) {
    if (points.size() < 2 || points.size() > 512)
        throw std::runtime_error("Polyline requires 2–512 sampled points");
    std::vector<Vec3> clean;
    for (auto point : points) {
        checkPoint(point);
        if (clean.empty() || length(point - clean.back()) >= tolerance)
            clean.push_back(point);
    }
    if (closed && clean.size() > 1 && length(clean.front() - clean.back()) < tolerance)
        clean.pop_back();
    if (clean.size() < (closed ? 3u : 2u))
        throw std::runtime_error("Polyline has too few distinct points");
    std::vector<std::array<Vec3, 2>> result;
    for (size_t i = 1; i < clean.size(); ++i)
        result.push_back({clean[i - 1], clean[i]});
    if (closed)
        result.push_back({clean.back(), clean.front()});
    return result;
}
} // namespace sketchy
