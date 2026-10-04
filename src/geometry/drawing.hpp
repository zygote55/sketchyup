#pragma once
#include "geometry/surface.hpp"
namespace sketchy {
struct DrawingPlane {
    Vec3 origin{}, normal{0, 0, 1}, xAxis{1, 0, 0}, yAxis{0, 1, 0};
    static DrawingPlane make(Vec3 origin, Vec3 normal, Vec3 xAxis);
    Vec3 point(double x, double y) const { return origin + xAxis * x + yAxis * y; }
    Vec3 coordinates(Vec3 point) const {
        auto d = point - origin;
        return {dot(d, xAxis), dot(d, yAxis), dot(d, normal)};
    }
};
std::vector<Vec3> rectangleOutline(const DrawingPlane &plane, double width, double height);
std::vector<Vec3> polygonOutline(const DrawingPlane &plane, double radius, unsigned sides);
std::vector<std::array<Vec3, 2>> polylineEdges(const std::vector<Vec3> &points, bool closed);
} // namespace sketchy
