#include "core/transform.hpp"
#include "geometry/solid_operations.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
Surface box(Vec3 origin = {}, Vec3 size = {2, 2, 2}) {
    Surface surface;
    const auto face =
        surface.addFace({{origin, origin + Vec3{size.x, 0, 0}, origin + Vec3{size.x, size.y, 0},
                          origin + Vec3{0, size.y, 0}}});
    surface.extrude(face, size.z);
    return surface;
}
void verify(const BooleanResult &result, double volume, size_t count, const Surface &target,
            const Surface &tool) {
    check(std::abs(result.volume - volume) < 1e-6 && result.parts.size() == count,
          "Solid operation has analytical volume and component count");
    double sum{};
    for (const auto &part : result.parts) {
        part.surface.validate();
        const auto report = inspectSolid(part.surface, Topology::rebuild(part.surface, {}));
        check(report.status == "solid" && report.volume &&
                  std::abs(*report.volume - part.volume) < 1e-6,
              "Each result independently validates native material volume");
        sum += *report.volume;
        check(part.sources.size() == part.surface.faces.size(), "Every output face has provenance");
        for (const auto &[face, source] : part.sources) {
            check(source.operand <= 1, "Source operand is original target/tool");
            const auto &original = source.operand ? tool : target;
            check(original.faces.contains(source.face), "Original source face exists");
            const auto normal = original.normal(source.face);
            const auto origin = original.vertices.at(original.faces.at(source.face).loops[0][0]);
            for (const auto &loop : part.surface.faces.at(face).loops)
                for (auto vertex : loop)
                    check(std::abs(dot(part.surface.vertices.at(vertex) - origin, normal)) <=
                              tolerance,
                          "Output lies on its original source face plane after operand remapping");
            const auto alignment = dot(part.surface.normal(face), normal);
            check(source.reversed ? alignment < -.999 : alignment > .999,
                  "Source reversal preserves material-side orientation");
        }
    }
    check(std::abs(sum - volume) < 1e-6, "Independent result volumes sum to analytical volume");
}
void splitVolumes(const SolidSplitResult &result, const Surface &a, const Surface &b, double target,
                  double tool, double overlap, size_t targetParts = 1, size_t toolParts = 1,
                  size_t overlapParts = 1) {
    verify(result.targetOnly, target, targetParts, a, b);
    verify(result.toolOnly, tool, toolParts, a, b);
    verify(result.overlap, overlap, overlapParts, a, b);
}
} // namespace
int main() {
    try {
        const auto a = box(), b = box({1, 0, 0});
        const auto originalA = a, originalB = b;
        auto split = splitSolids(a, b);
        splitVolumes(split, a, b, 4, 4, 4);
        const Surface *regions[]{&split.targetOnly.parts[0].surface,
                                 &split.toolOnly.parts[0].surface, &split.overlap.parts[0].surface};
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned j = i + 1; j < 3; ++j)
                check(booleanSolids(*regions[i], *regions[j], BooleanOperation::Intersect)
                          .parts.empty(),
                      "Split regions have no overlapping material interiors");
        check(a == originalA && b == originalB, "Split never mutates source geometry");
        verify(outerShellSolids(a, b), 12, 1, a, b);
        splitVolumes(splitSolids(a, a), a, a, 0, 0, 8, 0, 0, 1);
        verify(outerShellSolids(a, a), 8, 1, a, a);
        const auto disjoint = box({3, 0, 0});
        splitVolumes(splitSolids(a, disjoint), a, disjoint, 8, 8, 0, 1, 1, 0);
        verify(outerShellSolids(a, disjoint), 16, 2, a, disjoint);
        for (auto origin : {Vec3{2, 0, 0}, Vec3{2, 2, 0}, Vec3{2, 2, 2}}) {
            const auto contact = box(origin);
            splitVolumes(splitSolids(a, contact), a, contact, 8, 8, 0, 1, 1, 0);
            verify(outerShellSolids(a, contact), 16, origin.y == 0 ? 1 : 2, a, contact);
        }
        const auto inside = box({.5, .5, .5}, {1, 1, 1});
        splitVolumes(splitSolids(a, inside), a, inside, 7, 0, 1, 1, 0, 1);
        const auto hollow = booleanSolids(a, inside, BooleanOperation::Subtract).parts[0].surface;
        const auto island = box({.75, .75, .75}, {.5, .5, .5});
        const auto hollowBefore = hollow;
        auto filled = outerShellSolids(hollow, island);
        verify(filled, 8, 1, hollow, island);
        check(filled.parts[0].surface.faces.size() == 6 && hollow == hollowBefore,
              "Outer shell fills enclosed void and removes its material island without source "
              "mutation");
        for (const auto &[face, source] : filled.parts[0].sources)
            check(source.operand == 0, "Covered island faces do not survive outer shell");
        auto again = outerShellSolids(hollow, island);
        check(again.parts[0].surface == filled.parts[0].surface &&
                  again.parts[0].sources == filled.parts[0].sources,
              "Outer shell output geometry/provenance are deterministic");
        verify(outerShellSolids(hollow, disjoint), 16, 2, hollow, disjoint);
        const auto secondVoid = box({.1, .1, .1}, {.2, .2, .2});
        const auto twoVoids =
            booleanSolids(hollow, secondVoid, BooleanOperation::Subtract).parts[0].surface;
        verify(outerShellSolids(twoVoids, island), 8, 1, twoVoids, island);
        const auto through = box({.5, .5, -1}, {1, 1, 4});
        const auto tube = booleanSolids(a, through, BooleanOperation::Subtract).parts[0].surface;
        verify(outerShellSolids(tube, island), 6.125, 2, tube, island);
        const auto cutter = box({.75, -1, -1}, {.5, 4, 4});
        splitVolumes(splitSolids(a, cutter), a, cutter, 6, 6, 2, 2, 1, 1);
        for (unsigned i = 0; i < 6; ++i) {
            const auto frame = Transform::translation({800000, -700000, 600000}) *
                               Transform::rotation({1, double(i + 1), 3}, .19 + i * .23) *
                               Transform::scaling({-1.5, .75, 1.2});
            auto left = a, right = b, cavity = hollow, child = island;
            for (auto *surface : {&left, &right, &cavity, &child})
                for (auto &[id, point] : surface->vertices)
                    point = frame.point(point);
            splitVolumes(splitSolids(left, right), left, right, 5.4, 5.4, 5.4);
            verify(outerShellSolids(cavity, child), 10.8, 1, cavity, child);
        }
        auto invalid = b;
        invalid.faces.erase(invalid.faces.begin());
        for (bool outer : {false, true}) {
            try {
                if (outer)
                    outerShellSolids(a, invalid);
                else
                    splitSolids(a, invalid);
                throw std::runtime_error("Expected invalid solid rejection");
            } catch (const BooleanError &e) {
                check(e.code() == "BOOLEAN_INVALID_SOLID" && e.operand() == 1 &&
                          e.report().status == "open_boundary",
                      "Invalid operands retain original identity and boundary classification");
            }
        }
        std::cout << "Split partitions, joinery, outer-shell filling, islands/through-holes, "
                     "provenance, transforms and rejection passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
