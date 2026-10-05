#include "core/transform.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
Surface box(Vec3 origin, Vec3 size) {
    Surface surface;
    const auto face =
        surface.addFace({{origin, origin + Vec3{size.x, 0, 0}, origin + Vec3{size.x, size.y, 0},
                          origin + Vec3{0, size.y, 0}}});
    surface.extrude(face, size.z);
    return surface;
}
void reverse(Surface &surface) {
    for (auto &[id, face] : surface.faces)
        for (auto &loop : face.loops)
            std::reverse(loop.begin(), loop.end());
}
void append(Surface &target, Surface source, bool inward = false) {
    if (inward)
        reverse(source);
    for (const auto &[id, face] : source.faces) {
        std::vector<std::vector<Vec3>> loops;
        for (const auto &loop : face.loops) {
            loops.emplace_back();
            for (auto vertex : loop)
                loops.back().push_back(source.vertices.at(vertex));
        }
        target.addFace(loops);
    }
}
SolidShellAnalysis analyze(const Surface &surface) {
    surface.validate();
    return analyzeSolidShells(surface, Topology::rebuild(surface, {}));
}
SolidShellAnalysis valid(const Surface &surface, double volume, size_t count) {
    const auto before = surface;
    auto result = analyze(surface);
    check(result.report.status == "validated_shells",
          "Expected shell validation: " + result.report.status);
    check(result.report.volume &&
              std::abs(*result.report.volume - volume) < std::max(1e-11, volume * 1e-6),
          "Shell material volume has independent analytical expectation");
    check(result.shells.size() == count && surface == before,
          "Hierarchy size and source immutability");
    for (const auto &shell : result.shells) {
        check(!shell.faces.empty() && std::is_sorted(shell.faces.begin(), shell.faces.end()),
              "Stable source face identities retained per shell");
        check(bool(shell.parent) == bool(shell.depth), "Only roots have depth zero");
        if (shell.parent)
            check(result.shells[*shell.parent].depth + 1 == shell.depth,
                  "Each shell links its immediate containing parent");
    }
    return result;
}
void invalid(const Surface &surface, const std::string &expected) {
    const auto result = analyze(surface);
    check(result.report.status == expected,
          "Expected " + expected + ", got " + result.report.status);
    check(!result.report.volume && result.shells.empty(),
          "Failed analysis exposes no partial hierarchy/volume");
}
} // namespace
int main() {
    try {
        const auto outer = box({}, {4, 4, 4});
        auto cavity = outer;
        append(cavity, box({1, 1, 1}, {2, 2, 2}), true);
        auto result = valid(cavity, 56, 2);
        check(result.shells[0].depth == 0 && result.shells[1].parent == 0 &&
                  result.shells[0].signedVolume > 0 && result.shells[1].signedVolume < 0,
              "Inward enclosed shell is a cavity of the outer shell");
        check(inspectSolid(cavity, Topology::rebuild(cavity, {})).status == "solid",
              "A cavity and its outer boundary form one native material solid");
        auto reversed = cavity;
        reverse(reversed);
        result = valid(reversed, 56, 2);
        check(result.shells[0].signedVolume < 0 && result.shells[1].signedVolume > 0,
              "Globally reversed nested shell retains material interpretation");
        auto incorrectlyFilled = outer;
        append(incorrectlyFilled, box({1, 1, 1}, {2, 2, 2}));
        invalid(incorrectlyFilled, "inconsistent_winding");
        check(analyze(incorrectlyFilled).report.faces.size() == 2,
              "Nested winding defect identifies both source shells");
        auto island = box({}, {6, 6, 6});
        append(island, box({1, 1, 1}, {4, 4, 4}), true);
        append(island, box({2, 2, 2}, {2, 2, 2}));
        result = valid(island, 160, 3);
        check(result.shells[2].parent == 1 && result.shells[2].depth == 2,
              "Material island chooses nearest cavity boundary as parent");
        check(inspectSolid(island, Topology::rebuild(island, {})).status == "multiple_shells",
              "An island inside a cavity is a separate material component");
        auto twoCavities = box({}, {6, 6, 6});
        append(twoCavities, box({1, 1, 1}, {1, 1, 1}), true);
        append(twoCavities, box({4, 4, 4}, {1, 1, 1}), true);
        result = valid(twoCavities, 214, 3);
        check(result.shells[1].parent == 0 && result.shells[2].parent == 0,
              "Independent cavities share outer parent");
        auto disconnected = cavity;
        append(disconnected, box({8, 0, 0}, {1, 1, 1}), true);
        result = valid(disconnected, 57, 3);
        check(!result.shells[2].parent && result.shells[2].depth == 0,
              "Disconnected inward-wound solid is an independent material root");
        Surface tunnel;
        const auto cap = tunnel.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                         {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        tunnel.extrude(cap, 4);
        auto inTunnel = tunnel;
        append(inTunnel, box({1.5, 1.5, 1}, {1, 1, 1}));
        result = valid(inTunnel, 49, 2);
        check(!result.shells[1].parent,
              "A shell inside a through-hole is outside material despite its bounding box");
        append(tunnel, box({.2, .2, 1}, {.5, .5, .5}), true);
        valid(tunnel, 47.875, 2);
        for (unsigned i = 0; i < 12; ++i) {
            auto placed = cavity;
            const auto matrix = Transform::translation({800000, -700000, 600000}) *
                                Transform::rotation({double(i + 1), 2, 3}, .13 + i * .27) *
                                Transform::scaling({-1.3, .7, 1.2});
            for (auto &[id, p] : placed.vertices)
                p = matrix.point(p);
            valid(placed, 56 * 1.3 * .7 * 1.2, 2);
        }
        auto small = box({}, {.04, .04, .04});
        append(small, box({.01, .01, .01}, {.02, .02, .02}), true);
        valid(small, .000056, 2);
        auto crossing = outer;
        append(crossing, box({3, 1, 1}, {2, 2, 2}), true);
        invalid(crossing, "self_intersection");
        auto nearBoundary = outer;
        append(nearBoundary, box({tolerance * 2, 1, 1}, {1, 1, 1}), true);
        result = analyze(nearBoundary);
        check((result.report.status == "ambiguous_containment" ||
               result.report.status == "self_intersection") &&
                  !result.report.volume && result.shells.empty(),
              "Near-boundary containment is rejected conservatively");
        auto touching = outer;
        append(touching, box({0, 1, 1}, {1, 1, 1}), true);
        invalid(touching, "self_intersection");
        auto open = cavity;
        open.faces.erase(open.faces.begin());
        invalid(open, "open_boundary");
        auto loose = cavity;
        loose.vertex({8, 8, 8});
        invalid(loose, "loose_geometry");
        Surface many;
        for (unsigned i = 0; i < 65; ++i)
            append(many, box({double(i * 3), 0, 0}, {1, 1, 1}));
        invalid(many, "analysis_limit");
        result = valid(cavity, 56, 2);
        const auto again = analyze(cavity);
        for (size_t i = 0; i < result.shells.size(); ++i)
            check(result.shells[i].faces == again.shells[i].faces &&
                      result.shells[i].parent == again.shells[i].parent &&
                      result.shells[i].signedVolume == again.shells[i].signedVolume,
                  "Repeated hierarchy analysis is deterministic");
        std::cout << "Native shell containment, cavities/islands, winding, through-holes, "
                     "transformed volumes and bounded rejection passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
