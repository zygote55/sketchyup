#include "geometry/drawing.hpp"
#include "geometry/offset.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, const char *message, double eps = 1e-6) {
    check(std::abs(a - b) < eps, message);
}
std::vector<Vec3> box(double x, double y, double w, double h) {
    return {{x, y, 0}, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}};
}
double area(const OffsetResult &result) {
    double sum{};
    for (const auto &r : result.regions) {
        Surface s;
        auto f = s.addFace(r.loops);
        s.validate();
        sum += s.area(f);
    }
    return sum;
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const OffsetError &e) {
        check(e.code() == code, e.what());
        return;
    }
    throw std::runtime_error("Expected offset rejection: " + code);
}
} // namespace
int main() {
    try {
        Surface source;
        const auto f = source.addFace({box(0, 0, 4, 3)});
        const auto before = source;
        auto grow = offsetFaceRegion(source, f, .5), shrink = offsetFaceRegion(source, f, -.5);
        check(grow.regions.size() == 1 && shrink.regions.size() == 1,
              "Convex region stays connected");
        near(area(grow), 20, "Exact expanded rectangle area");
        near(area(shrink), 6, "Exact inset rectangle area");
        for (auto p : grow.regions[0].loops[0]) {
            check((std::abs(p.x + .5) < tolerance || std::abs(p.x - 4.5) < tolerance) &&
                      (std::abs(p.y + .5) < tolerance || std::abs(p.y - 3.5) < tolerance),
                  "Rectangle sides move exact distance");
        }
        const auto zero = offsetFaceRegion(source, f, 0);
        check(zero.regions[0].loops[0] == box(0, 0, 4, 3),
              "Zero offset preserves exact coordinates/order");
        check(offsetFaceRegion(source, f, -1.5).collapsed() &&
                  offsetFaceRegion(source, f, -2).collapsed(),
              "Complete erosion reports collapse");
        check(source == before,
              "All previews including collapse preserve input records and allocator");
        Surface ring;
        const auto r = ring.addFace({box(0, 0, 10, 8), box(3, 2, 4, 4)});
        auto outset = offsetFaceRegion(ring, r, 1), inset = offsetFaceRegion(ring, r, -.5);
        check(outset.inputHoles == 1 && outset.outputHoles == 1, "Hole counts reported");
        near(area(outset), 116, "Outset grows exterior and contracts hole");
        near(area(inset), 38, "Inset shrinks exterior and expands hole");
        const auto closed = offsetFaceRegion(ring, r, 2);
        check(closed.outputHoles == 0 && closed.regions.size() == 1,
              "Collapsed hole is explicitly reported");
        near(area(closed), 168, "Vanished hole leaves whole outer offset");
        check(offsetFaceRegion(ring, r, -2).collapsed(), "Expanded hole consumes ring");
        Surface ell;
        auto l = ell.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {1, 1, 0}, {1, 4, 0}, {0, 4, 0}}});
        near(area(offsetFaceRegion(ell, l, -.25)), 3.25, "Concave inset has exact area");
        near(area(offsetFaceRegion(ell, l, .25)), 11.25, "Concave outset has exact area");
        Surface neck;
        auto n = neck.addFace({{{0, 0, 0},
                                {3, 0, 0},
                                {3, 1, 0},
                                {5, 1, 0},
                                {5, 0, 0},
                                {8, 0, 0},
                                {8, 3, 0},
                                {5, 3, 0},
                                {5, 2, 0},
                                {3, 2, 0},
                                {3, 3, 0},
                                {0, 3, 0}}});
        auto split = offsetFaceRegion(neck, n, -.6);
        check(split.regions.size() == 2, "Collapsed neck retains both surviving islands");
        near(area(split), 6.48, "Split islands have expected total area");
        Surface twoHoles;
        auto th = twoHoles.addFace({box(0, 0, 12, 8), box(2, 3, 2, 2), box(5, 3, 2, 2)});
        auto merged = offsetFaceRegion(twoHoles, th, -.6);
        check(merged.regions.size() == 1 && merged.inputHoles == 2 && merged.outputHoles == 1,
              "Expanding holes merge without retaining an invalid thin wall");
        near(area(merged), 53.6, "Merged holes subtract the analytic union area");
        // Path start positions and hole ordering do not affect canonical output.
        auto reordered = twoHoles;
        auto &loops = reordered.faces.at(th).loops;
        std::swap(loops[1], loops[2]);
        for (auto &loop : loops)
            std::rotate(loop.begin(), loop.begin() + 1, loop.end());
        const auto canonical = offsetFaceRegion(twoHoles, th, .2);
        const auto reorderedResult = offsetFaceRegion(reordered, th, .2);
        check(canonical.regions.size() == reorderedResult.regions.size() &&
                  canonical.regions[0].loops == reorderedResult.regions[0].loops,
              "Canonical output ignores hole order and cyclic loop starts");
        Surface acute;
        const auto af = acute.addFace({{{0, 0, 0}, {10, 0, 0}, {0, .1, 0}}});
        const auto clipped = offsetFaceRegion(acute, af, .1);
        check(clipped.regions.size() == 1 && clipped.regions[0].loops[0].size() == 4,
              "Acute miter becomes a squared corner");
        for (auto p : clipped.regions[0].loops[0])
            check(p.x < 10.4 && p.x >= -.10000001 && p.y >= -.10000001,
                  "Acute corner stays bounded instead of creating a long spike");
        Surface collinear;
        const auto col =
            collinear.addFace({{{0, 0, 0}, {2, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        near(area(offsetFaceRegion(collinear, col, .5)), 20,
             "Redundant collinear vertices preserve the analytic offset");
        // Reversed face winding changes normal, never the material/void sign convention.
        auto reversed = box(0, 0, 4, 3);
        std::reverse(reversed.begin(), reversed.end());
        Surface rev;
        auto rf = rev.addFace({reversed});
        auto rr = offsetFaceRegion(rev, rf, .5);
        near(area(rr), 20, "Reversed winding still expands material");
        Surface checked;
        auto cf = checked.addFace(rr.regions[0].loops);
        check(dot(checked.normal(cf), rev.normal(rf)) > .999999,
              "Result retains source front orientation");
        // Rotated plane, large world origin, and non-grid continuous distances.
        const auto plane = DrawingPlane::make({900000, -850000, 700000}, {1, 2, 3}, {2, -1, 0});
        std::mt19937 rng(52);
        std::uniform_real_distribution<double> choose(.001, .49);
        for (int i = 0; i < 48; ++i) {
            double d = choose(rng);
            Surface tilted;
            std::vector<Vec3> points;
            for (auto p : box(0, 0, 4, 3))
                points.push_back(plane.point(p.x, p.y));
            const auto tf = tilted.addFace({points});
            auto out = offsetFaceRegion(tilted, tf, -d);
            near(area(out), (4 - 2 * d) * (3 - 2 * d), "Rotated continuous inset area", 2e-6);
            for (auto p : out.regions[0].loops[0]) {
                const auto q = plane.coordinates(p);
                near(q.z, 0, "Offset stays in oblique source plane", tolerance);
                check((std::abs(q.x - d) < 2e-8 || std::abs(q.x - (4 - d)) < 2e-8) &&
                          (std::abs(q.y - d) < 2e-8 || std::abs(q.y - (3 - d)) < 2e-8),
                      "Exact signed distance within 20 nm projection budget");
            }
        }
        Surface tiny;
        auto small = tiny.addFace({box(0, 0, 1e-6, 1e-6)});
        near(area(offsetFaceRegion(tiny, small, 1e-7)), 1.44e-12,
             "Micrometer geometry survives offset", 1e-15);
        rejects([&] { offsetFaceRegion(source, f, std::numeric_limits<double>::quiet_NaN()); },
                "INVALID_OFFSET_DISTANCE");
        rejects([&] { offsetFaceRegion(source, f, std::numeric_limits<double>::infinity()); },
                "INVALID_OFFSET_DISTANCE");
        rejects([&] { offsetFaceRegion(source, f, tolerance / 2); }, "INVALID_OFFSET_DISTANCE");
        rejects([&] { offsetFaceRegion(source, 999, .5); }, "INVALID_OFFSET_FACE");
        Surface far;
        auto ff = far.addFace({box(999999, 0, 1, 1)});
        rejects([&] { offsetFaceRegion(far, ff, 1); }, "OFFSET_RANGE");
        auto invalid = source;
        invalid.vertices.at(invalid.faces.at(f).loops[0][1]).z = .01;
        rejects([&] { offsetFaceRegion(invalid, f, .5); }, "INVALID_OFFSET_FACE");
        auto crossed = source;
        std::swap(crossed.faces.at(f).loops[0][1], crossed.faces.at(f).loops[0][2]);
        rejects([&] { offsetFaceRegion(crossed, f, .5); }, "INVALID_OFFSET_FACE");
        auto touching = ring;
        for (auto id : touching.faces.at(r).loops[1])
            touching.vertices.at(id).x -= 3;
        rejects([&] { offsetFaceRegion(touching, r, .5); }, "INVALID_OFFSET_FACE");
        auto wrongId = source;
        wrongId.faces.at(f).id = 0;
        rejects([&] { offsetFaceRegion(wrongId, f, 0); }, "INVALID_OFFSET_FACE");
        Surface many;
        many.nextId = 3000;
        std::vector<Id> loop;
        for (Id i = 1; i <= 1025; ++i) {
            loop.push_back(i);
            many.vertices[i] = {double(i), 0, 0};
        }
        many.faces[2000] = {2000, {loop}};
        rejects([&] { offsetFaceRegion(many, 2000, 1); }, "OFFSET_LIMIT");
        std::cout << "Planar offset convex/concave/holes, split/collapse, orientation, precision "
                     "and bounded rejection passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
