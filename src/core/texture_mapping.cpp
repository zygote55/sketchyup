#include "core/texture_mapping.hpp"
#include "geometry/drawing.hpp"
#include <limits>
#include <numbers>

namespace sketchy {
namespace {
constexpr double minimumGradient = 1e-9, maximumGradient = 1e9;
constexpr double minimumSine = 1e-10;
void validateCoordinate(TextureCoordinate value) {
    if (!std::isfinite(value.u) || !std::isfinite(value.v) || std::abs(value.u) > 1e9 ||
        std::abs(value.v) > 1e9)
        throw std::runtime_error(
            "Texture coordinates must be finite and within one billion repeats");
}
Vec3 gradientDirection(Vec3 gradient) {
    const auto magnitude = length(gradient);
    if (!std::isfinite(magnitude) || magnitude < minimumGradient || magnitude > maximumGradient)
        throw std::runtime_error(
            "Texture gradient length must be between 1e-9 and 1e9 repeats/metre");
    return gradient * (1 / magnitude);
}
} // namespace
void TextureMapping::validate() const {
    checkPoint(origin);
    validateCoordinate(offset);
    const auto u = gradientDirection(uGradient), v = gradientDirection(vGradient);
    if (length(cross(u, v)) < minimumSine)
        throw std::runtime_error("Texture gradients must be independent and numerically stable");
}
TextureCoordinate TextureMapping::coordinates(Vec3 point) const {
    validate();
    checkPoint(point);
    const auto delta = point - origin;
    // Do not wrap here: interpolating wrapped endpoints across a repeat boundary
    // changes the mapping. Image sampling alone applies repeat addressing.
    return {offset.u + dot(delta, uGradient), offset.v + dot(delta, vGradient)};
}
TextureMapping planarTextureMapping(Vec3 origin, Vec3 normal, Vec3 tangent, double width,
                                    double height, double rotationRadians,
                                    TextureCoordinate offset) {
    for (auto size : {width, height})
        if (!std::isfinite(size) || std::abs(size) < 1e-6 || std::abs(size) > 1e6)
            throw std::runtime_error(
                "Texture repeat size must be between 1 micrometre and 1 million metres");
    if (!std::isfinite(rotationRadians))
        throw std::runtime_error("Texture rotation must be finite");
    const auto plane = DrawingPlane::make(origin, normal, tangent);
    const auto angle = std::remainder(rotationRadians, 2 * std::numbers::pi);
    const auto c = std::cos(angle), s = std::sin(angle);
    TextureMapping result{origin, (plane.xAxis * c + plane.yAxis * s) * (1 / width),
                          (plane.yAxis * c - plane.xAxis * s) * (1 / height), offset};
    result.validate();
    return result;
}
TextureMapping pinnedTextureMapping(const std::array<Vec3, 3> &points,
                                    const std::array<TextureCoordinate, 3> &coordinates) {
    for (auto point : points)
        checkPoint(point);
    for (auto uv : coordinates)
        validateCoordinate(uv);
    const auto a = points[1] - points[0], b = points[2] - points[0];
    const auto al = length(a), bl = length(b);
    if (al < tolerance || bl < tolerance)
        throw std::runtime_error("Texture pins must be separated by the modeling tolerance");
    const auto aUnit = a * (1 / al), bUnit = b * (1 / bl);
    const auto area = cross(aUnit, bUnit);
    const auto sine = length(area);
    if (sine < minimumSine)
        throw std::runtime_error("Texture pins must define a numerically stable plane");
    const auto normal = area * (1 / sine);
    const auto dualA = cross(bUnit, normal) * (1 / (al * sine));
    const auto dualB = cross(normal, aUnit) * (1 / (bl * sine));
    TextureMapping result{points[0],
                          dualA * (coordinates[1].u - coordinates[0].u) +
                              dualB * (coordinates[2].u - coordinates[0].u),
                          dualA * (coordinates[1].v - coordinates[0].v) +
                              dualB * (coordinates[2].v - coordinates[0].v),
                          coordinates[0]};
    result.validate();
    return result;
}
TextureMapping transformTextureMapping(const TextureMapping &source, const Transform &oldToNew) {
    source.validate();
    const auto inverse = oldToNew.inverse();
    // Covectors transform with the inverse transpose. Transform::vector would
    // rotate correctly but stretch UVs incorrectly under scale or shear.
    auto covector = [&](Vec3 value) {
        return Vec3{dot({inverse.m[0], inverse.m[1], inverse.m[2]}, value),
                    dot({inverse.m[4], inverse.m[5], inverse.m[6]}, value),
                    dot({inverse.m[8], inverse.m[9], inverse.m[10]}, value)};
    };
    TextureMapping result{oldToNew.point(source.origin), covector(source.uGradient),
                          covector(source.vGradient), source.offset};
    result.validate();
    return result;
}
std::array<std::array<float, 2>, 3>
floatTextureCoordinates(const std::array<TextureCoordinate, 3> &coordinates,
                        TextureCoordinate maximumError) {
    if (!std::isfinite(maximumError.u) || !std::isfinite(maximumError.v) || maximumError.u <= 0 ||
        maximumError.v <= 0)
        throw std::runtime_error("Texture packing requires positive finite error bounds");
    for (const auto uv : coordinates)
        if (!std::isfinite(uv.u) || !std::isfinite(uv.v))
            throw std::runtime_error("Texture packing requires finite coordinates");
    const double u = std::floor(coordinates[0].u), v = std::floor(coordinates[0].v);
    std::array<std::array<float, 2>, 3> result;
    for (size_t corner = 0; corner < 3; ++corner) {
        const std::array<double, 2> value{coordinates[corner].u - u, coordinates[corner].v - v};
        const std::array<double, 2> error{maximumError.u, maximumError.v};
        for (size_t axis = 0; axis < 2; ++axis) {
            result[corner][axis] = float(value[axis]);
            const auto magnitude = std::abs(result[corner][axis]);
            const double spacing =
                std::nextafter(magnitude, std::numeric_limits<float>::infinity()) - magnitude;
            if (!std::isfinite(result[corner][axis]) ||
                std::abs(double(result[corner][axis]) - value[axis]) > error[axis] ||
                spacing * .5 > error[axis])
                throw std::runtime_error(
                    "Texture span exceeds supported float-coordinate precision");
        }
    }
    return result;
}
} // namespace sketchy
