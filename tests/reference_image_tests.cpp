#include "core/reference_image.hpp"
#include <iostream>
#include <limits>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-9) {
    check(std::abs(a - b) <= tolerance, "Reference calibration numerical mismatch");
}
void near(Vec3 a, Vec3 b) {
    near(a.x, b.x);
    near(a.y, b.y);
    near(a.z, b.z);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid reference image accepted");
}
void calibration(const Transform &local, const Transform &parent) {
    const ReferenceImage image{7, 4, 2, .35};
    const ImagePoint first{.2, .3}, second{.8, .7};
    const auto original = image;
    const auto world = parent * local;
    const auto anchor = world.point(referenceImagePoint(image, first));
    const auto result = calibrateReferenceImage(image, local, parent, first, second, 7.25);
    const auto newWorld = parent * result.local;
    near(newWorld.point(referenceImagePoint(result.image, first)), anchor);
    near(length(newWorld.vector(referenceImagePoint(result.image, second) -
                                referenceImagePoint(result.image, first))),
         7.25);
    near(result.image.width / result.image.height, 2);
    check(result.image.asset == 7 && result.image.opacity == .35 && image == original,
          "Calibration preserves source identity, opacity and input record");
    for (int i = 0; i < 12; ++i)
        check(result.local.m[i] == local.m[i], "Calibration preserves affine axes exactly");
    const auto again =
        calibrateReferenceImage(result.image, result.local, parent, first, second, 7.25);
    near(again.image.width, result.image.width);
    near(again.image.height, result.image.height);
    near((parent * again.local).point(referenceImagePoint(again.image, first)), anchor);
}
} // namespace
int main() {
    try {
        const ReferenceImage image{7, 4, 2, .35};
        image.validate();
        near(referenceImagePoint(image, {0, 0}), {0, 2, 0});
        near(referenceImagePoint(image, {1, 1}), {4, 0, 0});
        const auto corners = referenceImageCorners(image, Transform::translation({5, 6, 7}));
        near(corners[0], {5, 6, 7});
        near(corners[1], {9, 6, 7});
        near(corners[2], {9, 8, 7});
        near(corners[3], {5, 8, 7});
        calibration({}, {});
        calibration(Transform::translation({23, -13, 2}) *
                        Transform::rotation({1, 2, 3}, std::numbers::pi / 3),
                    Transform::translation({200, 300, 500}));
        auto shear = Transform::scaling({-3, 2, .5});
        shear.m[4] = .7;
        shear.m[9] = .4;
        calibration(Transform::rotation({1, 0, 0}, .4) * Transform::scaling({2, 3, 4}), shear);
        calibration(Transform::translation({250000, -250000, 50000}),
                    Transform::rotation({0, 0, 1}, .7));
        // An image may be used as a completely transparent reference without losing its placement.
        auto clear = image;
        clear.opacity = 0;
        clear.validate();
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        for (double bad : {0., -1., 1e-7, 1000001., nan}) {
            auto invalid = image;
            invalid.width = bad;
            rejects([&] { invalid.validate(); });
            invalid = image;
            invalid.height = bad;
            rejects([&] { invalid.validate(); });
            rejects([&] { calibrateReferenceImage(image, {}, {}, {0, 0}, {1, 1}, bad); });
        }
        for (double bad : {-1., 1.1, nan}) {
            auto invalid = image;
            invalid.opacity = bad;
            rejects([&] { invalid.validate(); });
            rejects([&] { referenceImagePoint(image, {bad, 0}); });
            rejects([&] { referenceImagePoint(image, {0, bad}); });
        }
        auto invalid = image;
        invalid.asset = 0;
        rejects([&] { invalid.validate(); });
        rejects([&] { calibrateReferenceImage(image, {}, {}, {.4, .4}, {.4, .4}, 1); });
        rejects([&] { calibrateReferenceImage(image, {}, {}, {0, 0}, {1e-9, 0}, 1); });
        rejects([&] { calibrateReferenceImage(image, {}, {}, {0, 0}, {.001, 0}, 1e6); });
        auto singular = Transform{};
        singular.m[0] = 0;
        rejects([&] { referenceImageCorners(image, singular); });
        rejects([&] { calibrateReferenceImage(image, singular, {}, {0, 0}, {1, 1}, 1); });
        rejects([&] { calibrateReferenceImage(image, {}, singular, {0, 0}, {1, 1}, 1); });
        rejects([&] { referenceImageCorners(image, Transform::translation({1e6, 0, 0})); });
        rejects([&] {
            calibrateReferenceImage(image, Transform::translation({999990, 0, 0}), {}, {0, 1},
                                    {1, 1}, 20);
        });
        std::cout
            << "Reference image coordinates, placement and anchored affine calibration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
