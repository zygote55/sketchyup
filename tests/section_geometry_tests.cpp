#include "geometry/section.hpp"
#include <algorithm>
#include <iostream>
#include <numbers>
#include <random>
using namespace sketchy;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message, double epsilon = 1e-6) {
    if (std::abs(actual - expected) > epsilon)
        throw std::runtime_error(std::string(message) + ": " + std::to_string(actual));
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected bounded section rejection");
}
std::vector<Triangle> box(Vec3 low, Vec3 high) {
    Surface s;
    const auto face = s.addFace({{{low.x, low.y, low.z},
                                  {high.x, low.y, low.z},
                                  {high.x, high.y, low.z},
                                  {low.x, high.y, low.z}}});
    s.extrude(face, high.z - low.z);
    return s.triangles();
}
double volume(const SectionMesh &mesh) {
    double result{};
    for (const auto &t : mesh.triangles)
        result += dot(t.vertices[0].point, cross(t.vertices[1].point, t.vertices[2].point)) / 6;
    return std::abs(result);
}
double capArea(const SectionMesh &mesh, Id section = 0) {
    double result{};
    for (const auto &t : mesh.triangles)
        if (t.section && (!section || t.section == section))
            result += length(cross(t.vertices[1].point - t.vertices[0].point,
                                   t.vertices[2].point - t.vertices[0].point)) *
                      .5;
    return result;
}
void verify(const SectionMesh &mesh, const std::vector<Triangle> &source,
            const std::vector<SectionCut> &cuts) {
    for (const auto &t : mesh.triangles) {
        for (const auto &v : t.vertices) {
            for (const auto &cut : cuts)
                check(cut.plane.distance(v.point) >= -2 * tolerance,
                      "Every result vertex belongs to retained half spaces");
            if (t.section) {
                check(t.source == noSectionSource && t.face == 0,
                      "Generated fill cannot impersonate a native face");
            } else {
                check(t.source < source.size() && t.face == source[t.source].face,
                      "Source triangle and native face identity retained");
                const auto &input = source[t.source];
                near(v.weights[0] + v.weights[1] + v.weights[2], 1, "Barycentric sum");
                check(*std::min_element(v.weights.begin(), v.weights.end()) >= -1e-12,
                      "Barycentric weights remain inside source");
                near(length(v.point - (input.a * v.weights[0] + input.b * v.weights[1] +
                                       input.c * v.weights[2])),
                     0, "Texture interpolation reconstructs clipped point");
            }
        }
        if (t.section) {
            const auto cut = std::find_if(cuts.begin(), cuts.end(),
                                          [&](const auto &c) { return c.id == t.section; });
            check(cut != cuts.end(), "Cap retains producing plane ID");
            const auto normal = cross(t.vertices[1].point - t.vertices[0].point,
                                      t.vertices[2].point - t.vertices[0].point);
            check(dot(normal, cut->plane.normal) < 0, "Cap faces removed half space");
        }
    }
    for (const auto &edge : mesh.edges)
        for (const auto &cut : cuts) {
            check(cut.plane.distance(edge.a) >= -2 * tolerance &&
                      cut.plane.distance(edge.b) >= -2 * tolerance,
                  "Section edges are clipped by later planes");
        }
}
} // namespace
int main() {
    try {
        auto source = box({0, 0, 0}, {2, 2, 2});
        const auto original = source;
        auto plain = sectionMesh(source, {});
        near(volume(plain), 8, "Uncut cube volume");
        check(plain.triangles.size() == source.size(), "Uncut mesh retains triangle count");
        const SectionCut x{1, {{1, 0, 0}, -1}};
        auto half = sectionMesh(source, {x});
        check(half.unfilledSections.empty(), "Closed cube produces a fill");
        near(volume(half), 4, "Half cube volume");
        near(capArea(half), 4, "Half cube cap area");
        verify(half, source, {x});
        const std::vector<SectionCut> octant{x, {2, {{0, 1, 0}, -1}}, {3, {{0, 0, 1}, -1}}};
        auto quarter = sectionMesh(source, octant);
        check(quarter.unfilledSections.empty(), "Successive cuts retain closed contours");
        near(volume(quarter), 1, "Three-plane intersection volume");
        near(capArea(quarter), 3, "Three final cap areas");
        verify(quarter, source, octant);
        auto reverse = octant;
        std::reverse(reverse.begin(), reverse.end());
        const auto reversed = sectionMesh(source, reverse);
        near(volume(reversed), volume(quarter), "Plane ordering preserves volume");
        near(capArea(reversed), capArea(quarter), "Plane ordering preserves fill area");
        const SectionCut diagonal{4, SectionPlane::through({1, 0, 0}, {1, 1, 1})};
        const auto corner = sectionMesh(source, {diagonal});
        near(volume(corner), 8 - 1.0 / 6, "Cut through existing vertices removes tetrahedron");
        check(corner.unfilledSections.empty(), "Vertex-aligned section fills");
        verify(corner, source, {diagonal});
        const auto tangent = sectionMesh(source, {{1, {{0, 0, 1}, 0}}});
        near(volume(tangent), 8, "Coincident existing face is retained");
        near(capArea(tangent), 0, "Coincident face receives no duplicate cap");
        check(tangent.edges.empty(), "No invented cut for tangent plane");
        check(sectionMesh(source, {{1, {{0, 0, 1}, -3}}}).triangles.empty(),
              "Fully removed mesh is empty");
        auto cavity = box({0, 0, 0}, {4, 4, 4});
        auto inner = box({1, 1, 1}, {3, 3, 3});
        for (auto t : inner) {
            std::swap(t.b, t.c);
            cavity.push_back(t);
        }
        const SectionCut z{5, {{0, 0, 1}, -2}};
        const auto hollow = sectionMesh(cavity, {z});
        check(hollow.unfilledSections.empty(), "Cavity contours form valid hole");
        near(capArea(hollow), 12, "Cavity remains empty in section fill");
        near(volume(hollow), 28, "Half hollow solid volume");
        verify(hollow, cavity, {z});
        auto island = box({1.5, 1.5, 1.5}, {2.5, 2.5, 2.5});
        cavity.insert(cavity.end(), island.begin(), island.end());
        const auto nested = sectionMesh(cavity, {z});
        near(capArea(nested), 13, "Nested material island inside cavity is filled");
        near(volume(nested), 28.5, "Nested island volume");
        auto other = box({5, 0, 0}, {6, 1, 4});
        cavity.insert(cavity.end(), other.begin(), other.end());
        const auto disconnected = sectionMesh(cavity, {z});
        near(capArea(disconnected), 14, "Disconnected contours both filled");
        verify(disconnected, cavity, {z});
        auto open = std::vector<Triangle>{{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, 42}};
        auto openResult = sectionMesh(open, {x});
        check(openResult.unfilledSections == std::vector<Id>{1} && !openResult.edges.empty() &&
                  capArea(openResult) == 0,
              "Open section keeps cut edges and reports absent fill");
        verify(openResult, open, {x});
        Transform shear;
        shear.m[4] = .35;
        const auto frame = Transform::translation({1234, -567, 89}) *
                           Transform::rotation({1, 2, 3}, .73) * shear *
                           Transform::scaling({-2, 3, .5});
        const auto worldPlane = x.plane.transformed(frame);
        near(worldPlane.distance(frame.point({1, .4, 1.2})), 0,
             "Inverse transpose retains oriented plane through shear/reflection");
        check(worldPlane.distance(frame.point({1.2, .4, 1.2})) > 0 &&
                  worldPlane.distance(frame.point({.8, .4, 1.2})) < 0,
              "Reflection retains the same local half space");
        auto transformed = source;
        for (auto &t : transformed) {
            t.a = frame.point(t.a);
            t.b = frame.point(t.b);
            t.c = frame.point(t.c);
            if (frame.determinant() < 0)
                std::swap(t.b, t.c);
        }
        auto world = sectionMesh(transformed, {{1, worldPlane}});
        check(world.unfilledSections.empty(), "Mirrored shear section contour closes");
        near(volume(world), 12, "Transformed section preserves determinant-scaled volume", 1e-4);
        verify(world, transformed, {{1, worldPlane}});
        auto segment = sectionSegment({0, 1.5, 1.5}, {2, 1.5, 1.5}, octant);
        check(segment && (*segment)[0] == Vec3{1, 1.5, 1.5}, "Wire clipping uses same half spaces");
        check(!sectionSegment({0, 0, 0}, {.5, .5, .5}, octant), "Removed wire rejected");
        check(sectionContains({1, 1, 1}, octant) && !sectionContains({.9, 1, 1}, octant),
              "Picking boundary agrees with mesh clipping");
        // Complementary cuts and plane-order changes test geometry invariants,
        // independently of the triangulator's particular diagonal choices.
        std::mt19937 random(0x53454354);
        std::uniform_real_distribution<double> component(-1, 1), position(.2, 1.8);
        for (unsigned sample = 0; sample < 80; ++sample) {
            auto n = normalized({component(random), component(random), component(random)});
            const auto p =
                SectionPlane::through({position(random), position(random), position(random)}, n);
            const SectionPlane opposite{p.normal * -1, -p.offset};
            const auto kept = sectionMesh(source, {{1, p}});
            const auto removed = sectionMesh(source, {{1, opposite}});
            check(kept.unfilledSections.empty() && removed.unfilledSections.empty(),
                  "Oblique complementary cuts both close");
            near(volume(kept) + volume(removed), 8, "Complementary clipped volumes conserve cube",
                 3e-6);
            near(capArea(kept), capArea(removed), "Complementary cap areas match", 3e-6);
            verify(kept, source, {{1, p}});
            const auto a = sectionMesh(source, {{1, p}, {2, {{0, 0, 1}, -.8}}});
            const auto b = sectionMesh(source, {{2, {{0, 0, 1}, -.8}}, {1, p}});
            check(a.unfilledSections.empty() && b.unfilledSections.empty(),
                  "Oblique multi-plane contours close in both orders");
            near(volume(a), volume(b), "Oblique cuts commute in volume", 3e-6);
            near(capArea(a), capArea(b), "Oblique cuts commute in cap area", 3e-6);
        }
        const auto tiny = box({0, 0, 0}, {.001, .001, .001});
        const auto smallCut = sectionMesh(tiny, {{1, {{1, 0, 0}, -.0005}}});
        near(volume(smallCut), .5e-9, "Millimetre feature volume", 1e-14);
        near(capArea(smallCut), 1e-6, "Millimetre feature cap", 1e-12);
        check(smallCut.unfilledSections.empty(), "Small feature contour closes");
        const auto large = box({999990, 999990, 999990}, {999992, 999992, 999992});
        const auto largeCut = sectionMesh(large, {{1, {{1, 0, 0}, -999991}}});
        near(capArea(largeCut), 4, "Large-coordinate section cap");
        check(largeCut.unfilledSections.empty(), "Large-coordinate contour closes");
        verify(largeCut, large, {{1, {{1, 0, 0}, -999991}}});
        for (double delta : {-1e-8, 0.0, 1e-8}) {
            const auto nearFace = sectionMesh(source, {{1, {{0, 0, 1}, delta}}});
            near(capArea(nearFace), 0, "Sub-tolerance plane does not invent duplicate face");
        }
        auto duplicated = source;
        duplicated.insert(duplicated.end(), source.begin(), source.end());
        const auto ambiguous = sectionMesh(duplicated, {x});
        check(ambiguous.unfilledSections == std::vector<Id>{1} && !ambiguous.edges.empty() &&
                  capArea(ambiguous) == 0,
              "Nonmanifold overlapping boundaries keep edges without invented fill");
        rejects([&] { sectionMesh(source, {{0, x.plane}}); });
        rejects([&] { sectionMesh(source, {x, x}); });
        rejects([&] {
            std::vector<SectionCut> tooMany;
            for (Id id = 1; id <= sectionPlaneLimit + 1; ++id)
                tooMany.push_back({id, x.plane});
            sectionMesh(source, tooMany);
        });
        rejects([&] { sectionMesh(source, {{1, {{0, 0, 2}, 0}}}); });
        rejects([&] { SectionPlane::through({}, {}); });
        rejects([&] { sectionContains({NAN, 0, 0}, {}); });
        rejects([&] { sectionMesh(std::vector<Triangle>(sectionInputTriangleLimit + 1), {}); });
        check(source.size() == original.size() && source[0].a == original[0].a &&
                  source[0].face == original[0].face,
              "Derived sections never edit native triangles");
        std::cout << "Oriented sections, holes, multiple cuts, provenance and bounds passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
