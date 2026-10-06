#include "core/texture_mapping.hpp"
#include <iostream>
#include <limits>
#include <numbers>

using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(TextureCoordinate actual, TextureCoordinate expected, const char *message,
          double epsilon = 1e-9) {
    check(std::isfinite(actual.u) && std::isfinite(actual.v) &&
              std::abs(actual.u - expected.u) <= epsilon &&
              std::abs(actual.v - expected.v) <= epsilon,
          message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("Expected invalid texture mapping to reject");
}
void planar() {
    const auto mapping =
        planarTextureMapping({10, 20, 30}, {0, 0, 3}, {2, 0, 7}, 2, 4, 0, {.25, -.5});
    near(mapping.coordinates({12, 24, 30}), {1.25, .5}, "Explicit size/offset oracle");
    near(mapping.coordinates({12, 24, -900}), {1.25, .5},
         "Projected mapping is independent of distance along the normal");
    near(mapping.coordinates({9, 30, 30}), {-.25, 2}, "UV remains unwrapped across repeats");
    const auto rotated = planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, 2, 4, std::numbers::pi / 2);
    near(rotated.coordinates({0, 2, 0}), {1, 0}, "Positive rotation moves U toward Y");
    near(rotated.coordinates({-4, 0, 0}), {0, 1}, "Quarter-turn V direction");
    const auto mirror = planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, -2, 4);
    near(mirror.coordinates({2, 4, 0}), {-1, 1}, "Independent texture-axis reflection");
    const auto back = planarTextureMapping({}, {0, 0, -1}, {1, 0, 0}, 2, 4);
    near(back.coordinates({2, 4, 0}), {1, -1}, "Opposite physical face frame");
    const auto oblique = planarTextureMapping({3, 4, 5}, {0, 1, 0}, {0, 0, 1}, .5, .25);
    near(oblique.coordinates({3.25, -17, 5.5}), {1, 1}, "Vertical projection frame oracle");
}
void pins() {
    const std::array<Vec3, 3> points{{{10, 20, 30}, {12, 20, 30}, {11, 24, 30}}};
    const std::array<TextureCoordinate, 3> uv{{{.25, -.5}, {2.25, .5}, {-.75, 2.5}}};
    const auto mapping = pinnedTextureMapping(points, uv);
    for (size_t i = 0; i < 3; ++i)
        near(mapping.coordinates(points[i]), uv[i], "Pins retain requested UV");
    // Half A plus quarter B, with an arbitrary projection-normal displacement.
    near(mapping.coordinates({11.25, 21, 999}), {1, .75}, "Independent sheared pin interpolation");
    const auto opposite =
        pinnedTextureMapping({points[0], points[2], points[1]}, {uv[0], uv[2], uv[1]});
    near(opposite.coordinates({11.25, 21, -999}), {1, .75}, "Reordering pins preserves projection");
    const auto identity = pinnedTextureMapping(
        {Vec3{}, Vec3{2, 0, 0}, Vec3{0, 4, 0}},
        {TextureCoordinate{}, TextureCoordinate{1, 0}, TextureCoordinate{0, 1}});
    check(identity == planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, 2, 4),
          "Orthogonal pins and explicit placement agree exactly");
}
void coordinateChanges() {
    const auto mapping = pinnedTextureMapping(
        {Vec3{1, 2, 3}, Vec3{3, 2, 3}, Vec3{2, 6, 3}},
        {TextureCoordinate{.25, -.5}, TextureCoordinate{2.25, .5}, TextureCoordinate{-.75, 2.5}});
    const auto before = mapping;
    Transform shear;
    shear.m[4] = .7;
    shear.m[8] = .3;
    shear.m[9] = -.4;
    shear.m[2] = .2;
    for (auto sign : {-1., 1.}) {
        const auto frame = Transform::translation({100000.125, 200000.25, 12.5}) *
                           Transform::rotation({2, -1, 1}, .7) * shear *
                           Transform::scaling({sign * 2, 3, .5});
        const auto converted = transformTextureMapping(mapping, frame);
        for (int x = -5; x <= 5; ++x)
            for (int y = -5; y <= 5; ++y)
                for (double z : {-17., 3., 25.}) {
                    const Vec3 point{1 + x * .4, 2 + y * .7, z};
                    near(converted.coordinates(frame.point(point)), mapping.coordinates(point),
                         "UV invariant under affine coordinate changes at site coordinates", 1e-9);
                }
        const auto restored = transformTextureMapping(converted, frame.inverse());
        near(restored.coordinates({7, -3, 4}), mapping.coordinates({7, -3, 4}),
             "Inverse coordinate conversion retains projection", 1e-9);
    }
    check(mapping == before, "Coordinate conversions preserve the source record");
    // Independent inverse-transpose oracle, including shear and reflection.
    Transform frame;
    frame.m[0] = -2;
    frame.m[4] = 1;
    frame.m[5] = 4;
    frame.m[10] = .5;
    const auto converted = transformTextureMapping(TextureMapping{}, frame);
    check(converted.uGradient == Vec3{-.5, .125, 0} && converted.vGradient == Vec3{0, .25, 0},
          "Gradients are covectors, not transformed tangent directions");
    near(converted.coordinates(frame.point({.3, .7, 90})), {.3, .7},
         "Reflected/sheared coordinate oracle");
}
void distantPrecision() {
    const Vec3 origin{800000.125, -700000.25, 600000.5};
    const auto mapping = planarTextureMapping(origin, {0, 0, 1}, {1, 0, 0}, .001, .002);
    near(mapping.coordinates(origin), {}, "Exact zero at distant anchor");
    near(mapping.coordinates(origin + Vec3{.00025, .0015, 0}), {.25, .75},
         "Sub-millimetre offsets remain usable at distant coordinates", 1e-7);
    // Affine UV evaluated at newly split face points follows barycentric
    // interpolation without per-vertex state or seams from early wrapping.
    const auto a = mapping.coordinates(origin + Vec3{-.002, .001, 0});
    const auto b = mapping.coordinates(origin + Vec3{.004, -.003, 0});
    near(mapping.coordinates(origin + Vec3{.001, -.001, 0}), {(a.u + b.u) / 2, (a.v + b.v) / 2},
         "Split-point affine interpolation", 1e-7);
}
void invalid() {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (auto size : {0., 1e-7, -1e-7, 1e7, -1e7, nan, inf}) {
        rejects([&] { planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, size, 1); });
        rejects([&] { planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, 1, size); });
    }
    for (auto size : {1e-6, -1e-6, 1e6, -1e6})
        planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, size, size).validate();
    rejects([&] { planarTextureMapping({}, {}, {1, 0, 0}, 1, 1); });
    rejects([&] { planarTextureMapping({}, {0, 0, 1}, {0, 0, 2}, 1, 1); });
    rejects([&] { planarTextureMapping({}, {0, 0, 1}, {nan, 0, 0}, 1, 1); });
    rejects([&] { planarTextureMapping({}, {0, 0, 1}, {1, 0, 0}, 1, 1, inf); });
    for (auto bad : std::vector<TextureMapping>{{{nan, 0, 0}},
                                                {{1e7, 0, 0}},
                                                {{}, {}, {0, 1, 0}},
                                                {{}, {inf, 0, 0}, {0, 1, 0}},
                                                {{}, {1e-10, 0, 0}, {0, 1, 0}},
                                                {{}, {1e10, 0, 0}, {0, 1, 0}},
                                                {{}, {1, 0, 0}, {1, 1e-11, 0}},
                                                {{}, {1, 0, 0}, {0, 1, 0}, {nan, 0}},
                                                {{}, {1, 0, 0}, {0, 1, 0}, {0, 1e10}}})
        rejects([&] { bad.coordinates({}); });
    rejects([&] { TextureMapping{}.coordinates({0, inf, 0}); });
    const std::array<TextureCoordinate, 3> uv{{{}, {1, 0}, {0, 1}}};
    rejects([&] { pinnedTextureMapping({Vec3{}, Vec3{}, Vec3{0, 1, 0}}, uv); });
    rejects([&] { pinnedTextureMapping({Vec3{}, Vec3{1, 0, 0}, Vec3{2, 0, 0}}, uv); });
    rejects([&] {
        pinnedTextureMapping(
            {Vec3{}, Vec3{1, 0, 0}, Vec3{0, 1, 0}},
            {TextureCoordinate{}, TextureCoordinate{1, 0}, TextureCoordinate{2, 0}});
    });
    Transform singular;
    singular.m[0] = 0;
    rejects([&] { transformTextureMapping(TextureMapping{}, singular); });
    rejects([&] { transformTextureMapping(TextureMapping{}, Transform::scaling({1e-10, 1, 1})); });
    rejects([&] {
        transformTextureMapping(TextureMapping{}, Transform::translation({1000000, 0, 0}) *
                                                      Transform::translation({1, 0, 0}));
    });
}
} // namespace
int main() {
    try {
        planar();
        pins();
        coordinateChanges();
        distantPrecision();
        invalid();
        std::cout << "Affine texture placement, pins, projection, reflection and "
                     "distant-coordinate invariants passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
