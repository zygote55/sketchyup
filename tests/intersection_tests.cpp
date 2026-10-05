#include "core/transform.hpp"
#include "geometry/intersection.hpp"
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
Surface face(std::vector<std::vector<Vec3>> loops) {
    Surface s;
    s.addFace(loops);
    return s;
}
FaceIntersection intersect(const Surface &a, const Surface &b) {
    return intersectFaces(a, a.faces.begin()->first, b, b.faces.begin()->first);
}
std::vector<Vec3> square(double x, double y, double w, double h) {
    return {{x, y, 0}, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}};
}
double total(const FaceIntersection &r) {
    double sum{};
    for (auto e : r.edges)
        sum += length(e[1] - e[0]);
    return sum;
}
void near(double a, double b, const char *message, double eps = 1e-7) {
    check(std::abs(a - b) < eps, message);
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const IntersectionError &e) {
        check(e.code() == code, e.what());
        return;
    }
    throw std::runtime_error("Expected classified intersection rejection");
}
} // namespace
int main() {
    try {
        const auto horizontal = face({square(0, 0, 4, 3)});
        const auto vertical = face({{{1, -1, -1}, {1, 4, -1}, {1, 4, 1}, {1, -1, 1}}});
        const auto hBefore = horizontal, vBefore = vertical;
        const auto crossed = intersect(horizontal, vertical);
        check(!crossed.coplanar && crossed.edges.size() == 1,
              "Crossing planes yield one finite interval");
        check(length(crossed.edges[0][0] - Vec3{1, 0, 0}) < tolerance &&
                  length(crossed.edges[0][1] - Vec3{1, 3, 0}) < tolerance,
              "Crossing endpoints independently match both face boundaries");
        const auto reverse = intersect(vertical, horizontal);
        near(total(reverse), 3, "Operand order preserves crossing geometry");
        const auto ring = face({square(0, 0, 4, 3), square(.5, 1, 2, 1)});
        const auto cut = intersect(ring, vertical);
        check(cut.edges.size() == 2, "A hole splits the intersection into two intervals");
        near(total(cut), 2, "Hole interior never becomes an intersection edge");
        const auto concave = face({{{0, 0, 0},
                                    {4, 0, 0},
                                    {4, 1, 0},
                                    {1, 1, 0},
                                    {1, 2, 0},
                                    {4, 2, 0},
                                    {4, 3, 0},
                                    {0, 3, 0}}});
        const auto vc = face({{{2, -1, -1}, {2, 4, -1}, {2, 4, 1}, {2, -1, 1}}});
        const auto concaveCut = intersect(concave, vc);
        check(concaveCut.edges.size() == 2, "Concave gaps produce disjoint line intervals");
        near(total(concaveCut), 2, "Concave cross sections retain the missing region");
        const auto overlapping = face({square(2, 1, 4, 3)});
        const auto overlap = intersect(horizontal, overlapping);
        check(overlap.coplanar && overlap.edges.size() == 4,
              "Coplanar overlap has common-region boundary");
        near(total(overlap), 8, "Common coplanar rectangle has analytic perimeter");
        const auto contained = face({square(1, 1, 1, 1)});
        near(total(intersect(horizontal, contained)), 4,
             "Contained face boundary subdivides larger region");
        const auto inHole = face({square(1, 1.2, .5, .5)});
        check(intersect(ring, inHole).edges.empty(),
              "Coplanar face inside hole has no material overlap");
        const auto touch = face({square(4, 1, 2, 1)});
        const auto contact = intersect(horizontal, touch);
        check(contact.coplanar && contact.edges.size() == 1,
              "Partial shared edge survives zero-area polygon clipping");
        near(total(contact), 1, "Shared-edge contact has exact finite length");
        check(intersect(horizontal, face({square(4, 3, 1, 1)})).edges.empty(),
              "Point-only contact does not invent a zero edge");
        rejects([&] { intersect(horizontal, face({square(4 - .5 * tolerance, 1, 1, 1)})); },
                "INTERSECTION_BELOW_TOLERANCE");
        auto apart = horizontal;
        apart.translate({0, 0, .01});
        check(intersect(horizontal, apart).edges.empty(),
              "Parallel disjoint faces have no intersection");
        auto flipped = overlapping;
        std::reverse(flipped.faces.begin()->second.loops[0].begin(),
                     flipped.faces.begin()->second.loops[0].end());
        near(total(intersect(horizontal, flipped)), 8,
             "Opposite front orientations retain overlap region");
        const auto same = intersect(horizontal, horizontal);
        check(same.edges.size() == 4, "Coincident face boundaries do not duplicate shared edges");
        near(total(same), 14, "Coincident rectangle perimeter is unchanged");
        const auto splitBoundary = face({{{0, 0, 0}, {2, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        const auto splitContact = intersect(horizontal, splitBoundary);
        check(splitContact.edges.size() == 4,
              "Redundant collinear vertices do not create overlapping output edges");
        near(total(splitContact), 14, "Collinear contact segments merge without double counting");
        for (int i = 1; i <= 16; ++i) {
            const auto t = Transform::translation({800000, -700000, 600000}) *
                           Transform::rotation({1, 2, 3}, i * .173);
            auto a = horizontal, b = vertical;
            for (auto &[id, p] : a.vertices)
                p = t.point(p);
            for (auto &[id, p] : b.vertices)
                p = t.point(p);
            const auto r = intersect(a, b);
            check(r.edges.size() == 1, "Oblique large-origin crossing remains one interval");
            near(total(r), 3, "Large origin preserves interval length", 2e-8);
            auto p = t.point({1, 0, 0}), q = t.point({1, 3, 0});
            check((length(r.edges[0][0] - p) < 2e-8 && length(r.edges[0][1] - q) < 2e-8) ||
                      (length(r.edges[0][0] - q) < 2e-8 && length(r.edges[0][1] - p) < 2e-8),
                  "Large-origin endpoints commute with rigid transform");
            b = overlapping;
            for (auto &[id, p] : b.vertices)
                p = t.point(p);
            near(total(intersect(a, b)), 8, "Oblique coplanar clipping preserves common perimeter",
                 1e-7);
        }
        auto tinyA = horizontal, tinyB = vertical;
        for (auto &[id, p] : tinyA.vertices)
            p = p * 1e-4;
        for (auto &[id, p] : tinyB.vertices)
            p = p * 1e-4;
        near(total(intersect(tinyA, tinyB)), 3e-4, "Submillimeter faces retain intersection length",
             1e-10);
        auto unstable = face({square(-100, -100, 200, 200)});
        const auto flat = unstable;
        const auto rotate = Transform::rotation({1, 0, 0}, 5e-9);
        for (auto &[id, p] : unstable.vertices)
            p = rotate.point(p);
        rejects([&] { intersect(flat, unstable); }, "INTERSECTION_UNSTABLE");
        const auto uncertainA =
            face({square(-tolerance, -tolerance, 2 * tolerance, 2 * tolerance)});
        const auto uncertainB = face({{{-tolerance, 0, -tolerance},
                                       {tolerance, 0, -tolerance},
                                       {tolerance, 0, tolerance},
                                       {-tolerance, 0, tolerance}}});
        rejects([&] { intersect(uncertainA, uncertainB); }, "INTERSECTION_UNSTABLE");
        auto invalid = horizontal;
        invalid.vertices.begin()->second.z = .1;
        rejects([&] { intersect(invalid, vertical); }, "INTERSECTION_INVALID_FACE");
        rejects([&] { intersectFaces(horizontal, 999, vertical, vertical.faces.begin()->first); },
                "INTERSECTION_INVALID_FACE");
        auto excessive = horizontal;
        excessive.faces.begin()->second.loops[0].resize(1025, 1);
        rejects([&] { intersect(excessive, vertical); }, "INTERSECTION_LIMIT");
        check(horizontal == hBefore && vertical == vBefore,
              "Intersection leaves input geometry and allocators unchanged");
        std::cout << "Crossing/coplanar face intersections, holes, contacts, oblique precision and "
                     "classified rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
