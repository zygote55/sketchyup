#include "core/transform.hpp"
#include <numbers>
namespace sketchy {
Transform Transform::translation(Vec3 delta) {
    checkPoint(delta);
    Transform result;
    result.m[12] = delta.x;
    result.m[13] = delta.y;
    result.m[14] = delta.z;
    return result;
}
Transform Transform::scaling(Vec3 scale) {
    Transform result;
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    result.validate();
    return result;
}
Transform Transform::rotation(Vec3 axis, double angleRadians) {
    if (!std::isfinite(angleRadians))
        throw std::runtime_error("Rotation angle must be finite");
    const auto n = normalized(axis);
    angleRadians = std::remainder(angleRadians, 2 * std::numbers::pi);
    const auto c = std::cos(angleRadians), s = std::sin(angleRadians), t = 1 - c;
    Transform result;
    result.m = {t * n.x * n.x + c,
                t * n.x * n.y + s * n.z,
                t * n.x * n.z - s * n.y,
                0,
                t * n.x * n.y - s * n.z,
                t * n.y * n.y + c,
                t * n.y * n.z + s * n.x,
                0,
                t * n.x * n.z + s * n.y,
                t * n.y * n.z - s * n.x,
                t * n.z * n.z + c,
                0,
                0,
                0,
                0,
                1};
    result.validate();
    return result;
}
Vec3 Transform::vector(Vec3 p) const {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z,
            m[2] * p.x + m[6] * p.y + m[10] * p.z};
}
Vec3 Transform::point(Vec3 p) const { return vector(p) + Vec3{m[12], m[13], m[14]}; }
Transform Transform::operator*(const Transform &rhs) const {
    Transform result;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            result.m[c * 4 + r] = 0;
            for (int k = 0; k < 4; ++k)
                result.m[c * 4 + r] += m[k * 4 + r] * rhs.m[c * 4 + k];
        }
    result.validate();
    return result;
}
double Transform::determinant() const {
    return dot({m[0], m[1], m[2]}, cross({m[4], m[5], m[6]}, {m[8], m[9], m[10]}));
}
void Transform::validate() const {
    for (auto value : m)
        if (!std::isfinite(value) || std::abs(value) > 1e12)
            throw std::runtime_error("Transform must contain bounded finite values");
    if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1)
        throw std::runtime_error("Expected affine transform");
    const auto product =
        length({m[0], m[1], m[2]}) * length({m[4], m[5], m[6]}) * length({m[8], m[9], m[10]});
    if (product == 0 || std::abs(determinant()) <= product * 1e-12)
        throw std::runtime_error("Singular or unstable transform");
}
Transform Transform::inverse() const {
    validate();
    const auto a = Vec3{m[0], m[1], m[2]}, b = Vec3{m[4], m[5], m[6]}, c = Vec3{m[8], m[9], m[10]};
    const auto inv = 1 / determinant();
    const auto r0 = cross(b, c) * inv, r1 = cross(c, a) * inv, r2 = cross(a, b) * inv;
    Transform result;
    result.m = {r0.x, r1.x, r2.x, 0, r0.y, r1.y, r2.y, 0, r0.z, r1.z, r2.z, 0, 0, 0, 0, 1};
    const auto t = result.vector({-m[12], -m[13], -m[14]});
    result.m[12] = t.x;
    result.m[13] = t.y;
    result.m[14] = t.z;
    result.validate();
    return result;
}
double meters(double value, std::string_view unit) {
    double factor;
    if (unit == "m")
        factor = 1;
    else if (unit == "mm")
        factor = .001;
    else if (unit == "cm")
        factor = .01;
    else if (unit == "in")
        factor = .0254;
    else if (unit == "ft")
        factor = .3048;
    else
        throw std::runtime_error("Unsupported length unit");
    const auto result = value * factor;
    if (!std::isfinite(result) || std::abs(result) > coordinateLimit)
        throw std::runtime_error("Length outside document bounds");
    return result;
}
double radians(double value, std::string_view unit) {
    if (unit != "rad" && unit != "deg")
        throw std::runtime_error("Unsupported angle unit");
    auto result = unit == "deg" ? value * std::numbers::pi / 180 : value;
    if (!std::isfinite(result))
        throw std::runtime_error("Angle must be finite");
    return result;
}
} // namespace sketchy
