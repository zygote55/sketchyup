#include "core/component_placement.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 actual, Vec3 expected, const char *message, double epsilon = 1e-8) {
    check(length(actual - expected) < epsilon, message);
}
template <class F> void rejects(const char *code, F operation) {
    try {
        operation();
    } catch (const PlacementError &error) {
        check(error.code() == code, error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
Surface square(bool hole = false) {
    Surface s;
    std::vector<std::vector<Vec3>> loops{{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}};
    if (hole)
        loops.push_back({{4, 4, 0}, {4, 6, 0}, {6, 6, 0}, {6, 4, 0}});
    s.addFace(loops);
    return s;
}
void containment() {
    auto host = square(true);
    const auto before = host;
    const auto face = host.faces.begin()->first;
    const DrawingPlane glue{};
    auto place = [&](Vec3 anchor) {
        return componentPlacementOnFace(host, face, {}, glue, {anchor});
    };
    auto result = place({2, 3, tolerance * .5});
    near(result.hostAnchor, {2, 3, 0}, "Plane tolerance snaps only along host normal");
    near(result.world.point({}), {2, 3, 0}, "Component glue origin maps to requested anchor");
    check(!result.boundary, "Interior anchor classified");
    for (auto point : std::vector<Vec3>{{0, 0, 0}, {10, 5, 0}, {4, 5, 0}, {6, 4, 0}})
        check(place(point).boundary,
              "Outer and hole boundaries are explicit eligible glue anchors");
    rejects("ANCHOR_OUTSIDE_FACE", [&] { place({5, 5, 0}); });
    rejects("ANCHOR_OUTSIDE_FACE", [&] { place({-1, 5, 0}); });
    rejects("ANCHOR_OFF_PLANE", [&] { place({2, 3, 2 * tolerance}); });
    check(host == before, "Placement never mutates native geometry or identities");
    std::reverse(host.faces.begin()->second.loops.back().begin(),
                 host.faces.begin()->second.loops.back().end());
    rejects("ANCHOR_OUTSIDE_FACE", [&] { place({5, 5, 0}); });
    near(place({2, 3, 0}).world.point({}), {2, 3, 0}, "Hole winding does not change containment");
}
void transformed() {
    const auto host = square();
    const auto face = host.faces.begin()->first;
    const auto source = DrawingPlane::make({1, 2, 3}, {0, -1, 0}, {1, 0, 0});
    const auto frame = Transform::translation({20, 30, 40}) *
                       Transform::rotation({0, 0, 1}, std::numbers::pi / 2) *
                       Transform::scaling({-2, 3, .5});
    const auto result = componentPlacementOnFace(
        host, face, frame, source, {{3, 4, 0}, {1, 0, 0}, std::numbers::pi / 2, {-1, 2, .5}});
    near(result.world.point(source.origin), {8, 24, 40},
         "Independent reflected host anchor oracle");
    near(result.hostFrame.normal, {0, 0, 1}, "Host reflection preserves physical outward normal");
    near(result.world.vector(source.xAxis), {-1, 0, 0},
         "Explicit rotation and reflected component X");
    near(result.world.vector(source.yAxis), {0, 2, 0},
         "Explicit component Y scale is not host scale");
    near(result.world.vector(source.normal), {0, 0, .5},
         "Component thickness scale independent of host");
    check(result.world.determinant() < 0, "Requested component reflection preserved");
    const auto inverse = result.world.inverse();
    near(inverse.point(result.hostFrame.origin), source.origin,
         "Glue point round trips through placement");
    Transform shear;
    shear.m[4] = .7;
    shear.m[8] = .3;
    shear.m[9] = -.4;
    shear.m[2] = .2;
    const auto a = componentPlacementOnFace(host, face, shear, {}, {{2, 3, 0}, {1, 0, 0}});
    const auto b = componentPlacementOnFace(host, face, shear, {}, {{2, 3, 0}, {1, 0, 7}});
    near(a.hostFrame.xAxis, b.hostFrame.xAxis,
         "Off-plane tangent component is removed before host shear");
    near(a.hostFrame.normal, normalized(Vec3{-.2, .14, 1}),
         "Independent inverse-transpose shear normal");
    near(a.world.vector({1, 0, 0}), normalized(Vec3{1, 0, .2}),
         "Host tangent remains in sheared plane");
    check(std::abs(dot(a.hostFrame.normal, a.hostFrame.xAxis)) < 1e-12 &&
              std::abs(a.world.determinant() - 1) < 1e-12,
          "Gluing frame remains orthonormal under host shear");
}
void scalesAndOrientation() {
    for (double scale : {1e-4, 1., 1000.}) {
        Surface host;
        const Vec3 offset{50000, -70000, 90000};
        const auto normal = normalized(Vec3{1, 2, 3});
        const auto plane = DrawingPlane::make(offset, normal, {1, 0, 0});
        const auto face = host.addFace({{plane.point(0, 0), plane.point(scale, 0),
                                         plane.point(scale, scale), plane.point(0, scale)}});
        const auto anchor = plane.point(scale * .3, scale * .6);
        for (double sign : {-1., 1.}) {
            const auto hostWorld = Transform::translation({-100, 250, -500}) *
                                   Transform::rotation({2, -1, 1}, .7) *
                                   Transform::scaling({sign * 2, 3, .5});
            const auto placed =
                componentPlacementOnFace(host, face, hostWorld, {}, {anchor, plane.xAxis});
            near(placed.world.point({}), hostWorld.point(anchor), "Oblique large-origin anchor",
                 1e-6);
            near(placed.world.vector({1, 0, 0}), normalized(hostWorld.vector(plane.xAxis)),
                 "Oblique host tangent", 1e-7);
            check(std::abs(placed.world.determinant() - 1) < 1e-8,
                  "Host scale/reflection never leaks into component size");
            const auto oldNormal = placed.hostFrame.normal;
            for (auto &loop : host.faces.at(face).loops)
                std::reverse(loop.begin(), loop.end());
            const auto reversed =
                componentPlacementOnFace(host, face, hostWorld, {}, {anchor, plane.xAxis});
            near(reversed.hostFrame.normal, oldNormal * -1,
                 "Explicit face reversal flips physical placement side", 1e-7);
            for (auto &loop : host.faces.at(face).loops)
                std::reverse(loop.begin(), loop.end());
        }
    }
}
void invalid() {
    const auto host = square();
    const auto face = host.faces.begin()->first;
    auto place = [&](DrawingPlane glue, FacePlacementOptions options,
                     Transform frame = Transform{}) {
        return componentPlacementOnFace(host, face, frame, glue, options);
    };
    rejects("INVALID_HOST_FACE", [&] { componentPlacementOnFace(host, 999, {}, {}, {{1, 1, 0}}); });
    rejects("INVALID_PLACEMENT", [&] { place({}, {{1, 1, 0}, {0, 0, 1}}); });
    rejects("INVALID_PLACEMENT", [&] { place({}, {{1, 1, 0}, {1, 0, 0}, 0, {1, 0, 1}}); });
    rejects("INVALID_PLACEMENT",
            [&] { place({}, {{1, 1, 0}, {1, 0, 0}, std::numeric_limits<double>::infinity()}); });
    auto glue = DrawingPlane{};
    glue.yAxis = {0, -1, 0};
    rejects("INVALID_GLUE_FRAME", [&] { place(glue, {{1, 1, 0}}); });
    glue = {};
    glue.normal = {0, 0, 2};
    rejects("INVALID_GLUE_FRAME", [&] { place(glue, {{1, 1, 0}}); });
    glue = {};
    glue.yAxis.x = std::numeric_limits<double>::quiet_NaN();
    rejects("INVALID_GLUE_FRAME", [&] { place(glue, {{1, 1, 0}}); });
    Transform singular;
    singular.m[0] = 0;
    rejects("INVALID_PLACEMENT", [&] { place({}, {{1, 1, 0}}, singular); });
    Surface invalid = host;
    invalid.faces.at(face).loops[0][1] = 999;
    rejects("INVALID_HOST_FACE",
            [&] { componentPlacementOnFace(invalid, face, {}, {}, {{1, 1, 0}}); });
    Surface oversized;
    oversized.faces[1] = {1, {std::vector<Id>(4097, 1)}};
    rejects("PLACEMENT_LIMIT",
            [&] { componentPlacementOnFace(oversized, 1, {}, {}, {{1, 1, 0}}); });
    auto twisted = host;
    twisted.vertices.at(twisted.faces.at(face).loops[0][2]).z = 1;
    rejects("INVALID_HOST_FACE",
            [&] { componentPlacementOnFace(twisted, face, {}, {}, {{1, 1, 0}}); });
}
} // namespace
int main() {
    try {
        containment();
        transformed();
        scalesAndOrientation();
        invalid();
        std::cout << "Bounded face gluing, containment, physical orientation and explicit "
                     "component scale passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
