#include "core/transform.hpp"
#include "geometry/boolean.hpp"
#include <algorithm>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double value, double expected, const char *message, double eps = 1e-6) {
    check(std::abs(value - expected) < eps, message);
}
Surface box(Vec3 origin = {}, Vec3 size = {2, 2, 2}) {
    Surface s;
    const auto f = s.addFace({{origin, origin + Vec3{size.x, 0, 0},
                               origin + Vec3{size.x, size.y, 0}, origin + Vec3{0, size.y, 0}}});
    s.extrude(f, size.z);
    return s;
}
void verify(const BooleanResult &result, double volume, size_t count) {
    near(result.volume, volume, "Boolean analytical volume");
    check(result.parts.size() == count, "Boolean disconnected part count");
    for (const auto &part : result.parts) {
        check(part.sources.size() == part.surface.faces.size(),
              "Every output face has source provenance");
        auto topology = Topology::rebuild(part.surface, {});
        topology.validate(part.surface);
        auto report = inspectSolid(part.surface, topology);
        check(report.status == "solid", "Native Boolean output independently classifies solid");
        near(*report.volume, part.volume, "Native and adapter volumes agree");
    }
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const BooleanError &e) {
        if (e.code() != code)
            throw std::runtime_error("Expected " + code + ", got " + e.code() + ": " + e.what());
        return;
    }
    throw std::runtime_error("Expected " + code);
}
} // namespace
int main() {
    try {
        const auto a = box(), b = box({1, 0, 0}), originalA = a, originalB = b;
        auto united = booleanSolids(a, b, BooleanOperation::Union);
        verify(united, 12, 1);
        auto sub = booleanSolids(a, b, BooleanOperation::Subtract);
        verify(sub, 4, 1);
        verify(booleanSolids(a, b, BooleanOperation::Intersect), 4, 1);
        check(a == originalA && b == originalB, "Operands remain immutable");
        bool flippedTool{};
        for (const auto &[id, source] : sub.parts[0].sources) {
            const auto &input = source.operand ? b : a;
            check(input.faces.contains(source.face), "Provenance names an existing source face");
            const auto alignment = dot(sub.parts[0].surface.normal(id), input.normal(source.face));
            check(source.reversed ? alignment < -.999 : alignment > .999,
                  "Material side mapping tracks reversed cutting faces");
            flippedTool |= source.operand == 1 && source.reversed;
        }
        check(flippedTool, "Subtraction retains reversed tool face provenance");
        auto again = booleanSolids(a, b, BooleanOperation::Union);
        check(united.parts[0].surface == again.parts[0].surface &&
                  united.parts[0].sources == again.parts[0].sources,
              "Repeated result IDs and provenance are deterministic");
        const auto disjoint = box({3, 0, 0});
        verify(booleanSolids(a, disjoint, BooleanOperation::Union), 16, 2);
        verify(booleanSolids(a, disjoint, BooleanOperation::Subtract), 8, 1);
        verify(booleanSolids(a, disjoint, BooleanOperation::Intersect), 0, 0);
        const auto touching = box({2, 0, 0});
        verify(booleanSolids(a, touching, BooleanOperation::Union), 16, 1);
        verify(booleanSolids(a, touching, BooleanOperation::Subtract), 8, 1);
        verify(booleanSolids(a, touching, BooleanOperation::Intersect), 0, 0);
        verify(booleanSolids(a, a, BooleanOperation::Union), 8, 1);
        verify(booleanSolids(a, a, BooleanOperation::Subtract), 0, 0);
        verify(booleanSolids(a, a, BooleanOperation::Intersect), 8, 1);
        const auto corner = box({2, 2, 2});
        verify(booleanSolids(a, corner, BooleanOperation::Union), 16, 2);
        const auto cutter = box({.75, -1, -1}, {.5, 4, 4});
        verify(booleanSolids(a, cutter, BooleanOperation::Subtract), 6, 2);
        const auto inside = box({.5, .5, .5}, {1, 1, 1});
        verify(booleanSolids(a, inside, BooleanOperation::Union), 8, 1);
        verify(booleanSolids(a, inside, BooleanOperation::Intersect), 1, 1);
        rejects([&] { booleanSolids(a, inside, BooleanOperation::Subtract); }, "BOOLEAN_CAVITY");
        auto reversed = a;
        for (auto &[id, face] : reversed.faces)
            for (auto &loop : face.loops)
                std::reverse(loop.begin(), loop.end());
        verify(booleanSolids(reversed, b, BooleanOperation::Union), 12, 1);
        // A through-cut reconstructs actual polygon holes, not triangulation diagonals.
        auto through = box({.5, .5, -1}, {1, 1, 4});
        auto tube = booleanSolids(a, through, BooleanOperation::Subtract);
        verify(tube, 6, 1);
        size_t holes{};
        for (const auto &[id, face] : tube.parts[0].surface.faces)
            holes += face.loops.size() - 1;
        check(holes == 2, "Through-cut retains two native face holes");
        for (int i = 0; i < 8; ++i) {
            auto transform = Transform::translation({800000, -700000, 600000}) *
                             Transform::rotation({1, 2, 3}, .11 * i);
            auto left = a, right = b;
            for (auto &[id, p] : left.vertices)
                p = transform.point(p);
            for (auto &[id, p] : right.vertices)
                p = transform.point(p);
            verify(booleanSolids(left, right, BooleanOperation::Union), 12, 1);
            verify(booleanSolids(left, right, BooleanOperation::Subtract), 4, 1);
            verify(booleanSolids(left, right, BooleanOperation::Intersect), 4, 1);
        }
        auto mirror = Transform::translation({20, -10, 4}) * Transform::rotation({2, 1, 3}, .37) *
                      Transform::scaling({-1.5, .75, 1.2});
        auto mirroredA = a, mirroredB = b;
        for (auto &[id, p] : mirroredA.vertices)
            p = mirror.point(p);
        for (auto &[id, p] : mirroredB.vertices)
            p = mirror.point(p);
        verify(booleanSolids(mirroredA, mirroredB, BooleanOperation::Union), 16.2, 1);
        verify(booleanSolids(mirroredA, mirroredB, BooleanOperation::Subtract), 5.4, 1);
        verify(booleanSolids(mirroredA, mirroredB, BooleanOperation::Intersect), 5.4, 1);
        verify(booleanSolids(tube.parts[0].surface, disjoint, BooleanOperation::Union), 14, 2);
        verify(booleanSolids(a, box({2, 2, 0}), BooleanOperation::Union), 16, 2);
        verify(booleanSolids(box({}, {.01, .01, .01}), box({.005, 0, 0}, {.01, .01, .01}),
                             BooleanOperation::Union),
               1.5e-6, 1);
        Surface cylinder;
        std::vector<Vec3> circle;
        for (int i = 0; i < 16; ++i) {
            const auto angle = 2 * std::numbers::pi * i / 16;
            circle.push_back({.02 * std::cos(angle), .02 * std::sin(angle), 0});
        }
        const auto cf = cylinder.addFace({circle});
        const auto cylinderVolume = cylinder.area(cf) * .1;
        cylinder.extrude(cf, .1);
        verify(booleanSolids(cylinder, disjoint, BooleanOperation::Union), 8 + cylinderVolume, 2);
        auto split = a;
        const auto topology = Topology::rebuild(split, {});
        splitEdge(split, topology.edges.begin()->second, .3);
        verify(booleanSolids(split, b, BooleanOperation::Union), 12, 1);
        auto nearPlane = box({1, 0, 8 * tolerance});
        near(booleanSolids(a, nearPlane, BooleanOperation::Intersect).volume, 4 - 16 * tolerance,
             "Near-coplanar overlap retains expected volume", 2e-6);
        rejects(
            [&] { booleanSolids(a, box({2 - .5 * tolerance, 0, 0}), BooleanOperation::Intersect); },
            "BOOLEAN_OUTPUT");
        auto oversized = a;
        for (Id i = 100; i < 4200; ++i)
            oversized.vertices[i] = {double(i), 0, 0};
        rejects([&] { booleanSolids(oversized, b, BooleanOperation::Union); }, "BOOLEAN_LIMIT");
        auto huge = box({}, {100000, 100000, 100000});
        rejects([&] { booleanSolids(huge, huge, BooleanOperation::Union); }, "BOOLEAN_PRECISION");
        Surface open;
        open.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        try {
            booleanSolids(a, open, BooleanOperation::Union);
            check(false, "Open operand must reject");
        } catch (const BooleanError &e) {
            check(e.code() == "BOOLEAN_INVALID_SOLID" && e.operand() == 1 &&
                      e.report().status == "open_boundary" && !e.report().edges.empty(),
                  "Invalid solid identifies operand and boundary defects before mutation");
        }
        std::cout << "Boolean volumes, contacts, disconnected parts, holes, provenance, precision "
                     "and atomic rejection passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
