#pragma once
#include "geometry/surface.hpp"
#include <string_view>
namespace sketchy {
struct Transform {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static Transform translation(Vec3 delta);
    static Transform scaling(Vec3 scale);
    Transform operator*(const Transform &rhs) const;
    Vec3 point(Vec3 value) const;
    Vec3 vector(Vec3 value) const;
    Transform inverse() const;
    double determinant() const;
    void validate() const;
    bool operator==(const Transform &) const = default;
};
double meters(double value, std::string_view unit);
double radians(double value, std::string_view unit);
} // namespace sketchy
