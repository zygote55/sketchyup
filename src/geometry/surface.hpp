#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace sketchy {
using Id = std::uint64_t;
constexpr double tolerance = 1e-7;
constexpr double coordinateLimit = 1e6;
struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    bool operator==(const Vec3 &) const = default;
};
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 normalized(Vec3 v);
void checkPoint(Vec3 v);
struct Face {
    Id id{};
    std::vector<std::vector<Id>> loops;
    bool operator==(const Face &) const = default;
};
struct Triangle {
    Vec3 a, b, c;
    Id face{};
};
struct Edge {
    Id a{}, b{};
    std::vector<Id> faces;
};
struct Surface {
    std::map<Id, Vec3> vertices;
    std::map<Id, Face> faces;
    // Wire edges are authoritative too; face adjacency is derived from ordered loops.
    std::vector<std::array<Id, 2>> wires;
    Id nextId{1};
    Id vertex(Vec3 point);
    Id addFace(const std::vector<std::vector<Vec3>> &loops);
    Id addFaceIds(std::vector<std::vector<Id>> loops);
    Vec3 normal(Id face) const;
    std::vector<Triangle> triangulate(Id face) const;
    std::vector<Triangle> triangles() const;
    std::vector<Edge> edges() const;
    void validate() const;
    double area(Id face) const;
    // Extrudes an isolated planar face into a prism, retaining the base face ID.
    // Adjacent-face push/pull is intentionally a later topology operation.
    Id extrude(Id face, double distance);
    void translate(Vec3 delta);
    bool operator==(const Surface &) const = default;
};
} // namespace sketchy
