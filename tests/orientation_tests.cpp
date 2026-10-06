#include "core/transform.hpp"
#include "geometry/orientation.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Surface box(Vec3 o = {}) {
    Surface s;
    const auto face = s.addFace({{o, o + Vec3{2, 0, 0}, o + Vec3{2, 2, 0}, o + Vec3{0, 2, 0}}});
    s.extrude(face, 2);
    return s;
}
void consistent(const Surface &s, const Topology &topology) {
    s.validate();
    topology.validate(s);
    for (const auto &[edge, faces] : topology.adjacency(s).edgeFaces)
        check(faces.size() < 2 || (faces.size() == 2 && faces[0].reversed != faces[1].reversed),
              "All two-sided edges have opposite face incidence directions");
}
void unchangedGeometry(const Surface &before, const FaceOrientationResult &after,
                       const Topology &topology) {
    check(after.surface.vertices == before.vertices && after.surface.wires == before.wires &&
              after.surface.nextId == before.nextId &&
              after.surface.faces.size() == before.faces.size(),
          "Reorientation preserves all geometry identities and allocator state");
    check(Topology::rebuild(after.surface, topology) == topology,
          "Reorientation preserves every authoritative edge and edge allocator");
    for (const auto &[id, face] : before.faces) {
        const bool reversed = std::binary_search(after.reversed.begin(), after.reversed.end(), id);
        check(std::abs(dot(before.normal(id), after.surface.normal(id)) - (reversed ? -1 : 1)) <
                  1e-10,
              "Reported faces have exactly reversed normals");
        check(std::abs(before.area(id) - after.surface.area(id)) < 1e-7,
              "Reversal preserves face area and hole coverage");
    }
}
template <class F> OrientationError rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const OrientationError &e) {
        check(e.code() == code, "Classified orientation error");
        return e;
    }
    throw std::runtime_error("Expected orientation rejection");
}
Surface mobius() {
    Surface s;
    constexpr size_t count = 12;
    std::array<std::array<Id, 2>, count> ids{};
    for (size_t i = 0; i < count; ++i) {
        const double angle = 2 * std::numbers::pi * i / count;
        for (size_t j = 0; j < 2; ++j) {
            const double t = j ? .3 : -.3;
            ids[i][j] = s.vertex({(2 + t * std::cos(angle / 2)) * std::cos(angle),
                                  (2 + t * std::cos(angle / 2)) * std::sin(angle),
                                  t * std::sin(angle / 2)});
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        const auto a = ids[i][0], b = ids[i][1], c = ids[next][next ? 1 : 0],
                   d = ids[next][next ? 0 : 1];
        s.addFaceIds({{a, b, c}});
        s.addFaceIds({{a, c, d}});
    }
    return s;
}
} // namespace
int main() {
    try {
        const auto cube = box();
        const auto topology = Topology::rebuild(cube, {});
        const auto seed = cube.faces.begin()->first;
        std::set<Id> all;
        for (const auto &[id, face] : cube.faces)
            all.insert(id);
        const auto reversed = reverseFaces(cube, topology, all);
        unchangedGeometry(cube, reversed, topology);
        consistent(reversed.surface, topology);
        check(reverseFaces(reversed.surface, topology, all).surface == cube,
              "Double reversal restores byte-equivalent surface ordering");
        check(orientFaces(cube, topology, seed).reversed.empty(),
              "Consistent connected surface returns an explicit no-change result");
        const auto inward = orientFaces(reversed.surface, topology, seed);
        check(inward.reversed.empty() && inward.surface == reversed.surface,
              "Orient preserves an inward seed; it never guesses outwardness");
        // Exhaust every face winding combination on a closed cube. The reference
        // normal determines the only two possible consistent orientations.
        for (unsigned mask = 0; mask < 64; ++mask) {
            std::set<Id> flipped;
            size_t i{};
            for (auto face : all)
                if (mask & (1u << i++))
                    flipped.insert(face);
            const auto input =
                flipped.empty() ? cube : reverseFaces(cube, topology, flipped).surface;
            const auto saved = input;
            const auto result = orientFaces(input, topology, seed);
            check(input == saved && result.connected.size() == 6,
                  "Orientation is immutable and visits exactly the connected shell");
            unchangedGeometry(input, result, topology);
            consistent(result.surface, topology);
            for (auto id : all)
                check(dot(result.surface.normal(id), cube.normal(id)) > .999 == !(mask & 1),
                      "Every face matches the seed-determined outward or inward solution");
            const auto solid = inspectSolid(result.surface, topology);
            check(solid.status == "solid" && solid.volume && std::abs(*solid.volume - 8) < 1e-8,
                  "Repaired shell has independently expected material volume");
            check(orientFaces(input, topology, seed).surface == result.surface,
                  "Repeated orientation is deterministic");
        }
        Surface sheet;
        const auto left = sheet.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto right = sheet.addFace({{{1, 0, 0}, {1, 1, 0}, {2, 1, 0}, {2, 0, 0}}});
        const auto isolated = sheet.addFace({{{4, 0, 0}, {5, 0, 0}, {5, 1, 0}, {4, 1, 0}}});
        const auto wireStart = sheet.vertex({1, 1, 0}), wireEnd = sheet.vertex({4, 0, 0});
        sheet.wires.push_back({wireStart, wireEnd});
        const auto sheetTopology = Topology::rebuild(sheet, {});
        const auto open = orientFaces(sheet, sheetTopology, left);
        check(open.reversed == std::vector<Id>{right} &&
                  open.connected == std::vector<Id>{left, right} &&
                  open.surface.faces.at(isolated) == sheet.faces.at(isolated),
              "Open sheets orient across shared edges; wires never connect separate faces");
        unchangedGeometry(sheet, open, sheetTopology);
        consistent(open.surface, sheetTopology);
        Surface holed;
        const auto ring = holed.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                         {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        holed.extrude(ring, 1);
        const auto holeTopology = Topology::rebuild(holed, {});
        const auto badHole = reverseFaces(holed, holeTopology, {ring});
        const auto holeResult =
            orientFaces(badHole.surface, holeTopology, holed.faces.rbegin()->first);
        check(holeResult.surface == holed && holeResult.reversed == std::vector<Id>{ring},
              "Holed face reverses every loop and restores the through-hole boundary");
        unchangedGeometry(badHole.surface, holeResult, holeTopology);
        consistent(holeResult.surface, holeTopology);
        // Two material shells have no shared edge: orienting one cavity must not
        // silently flip the independent outer shell or change shell containment.
        auto cavity = cube;
        auto inner = box({.5, .5, .5});
        for (auto &[id, point] : inner.vertices)
            point = (point - Vec3{.5, .5, .5}) * .5 + Vec3{.5, .5, .5};
        std::set<Id> innerFaces;
        for (const auto &[id, face] : inner.faces) {
            std::vector<std::vector<Vec3>> loops;
            for (const auto &loop : face.loops) {
                loops.emplace_back();
                for (auto vertex : loop)
                    loops.back().push_back(inner.vertices.at(vertex));
                std::reverse(loops.back().begin(), loops.back().end());
            }
            innerFaces.insert(cavity.addFace(loops));
        }
        const auto cavityTopology = Topology::rebuild(cavity, {});
        const auto damaged = reverseFaces(cavity, cavityTopology, {*innerFaces.rbegin()});
        const auto cavityResult = orientFaces(damaged.surface, cavityTopology, *innerFaces.begin());
        check(cavityResult.surface == cavity && cavityResult.connected.size() == 6,
              "Cavity orientation affects only its edge-connected boundary");
        check(inspectSolid(cavityResult.surface, cavityTopology).volume == std::optional<double>(7),
              "Restored cavity retains material volume");
        Surface junction;
        const auto first = junction.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        junction.addFace({{{1, 0, 0}, {0, 0, 0}, {0, -1, 0}}});
        junction.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}});
        const auto unrelated = junction.addFace({{{3, 0, 0}, {4, 0, 0}, {3, 1, 0}}});
        const auto junctionTopology = Topology::rebuild(junction, {});
        const auto error = rejects([&] { orientFaces(junction, junctionTopology, first); },
                                   "ORIENTATION_NON_MANIFOLD");
        check(error.edge() && error.face() && junctionTopology.edges.contains(error.edge()),
              "Non-manifold error identifies native face and edge");
        check(orientFaces(junction, junctionTopology, unrelated).connected ==
                  std::vector<Id>{unrelated},
              "Unrelated non-manifold component does not block orienting an independent sheet");
        const auto band = mobius();
        const auto bandTopology = Topology::rebuild(band, {});
        const auto conflict =
            rejects([&] { orientFaces(band, bandTopology, band.faces.begin()->first); },
                    "ORIENTATION_CONFLICT");
        check(conflict.edge() && conflict.face(),
              "Mobius strip reports contradictory orientation cycle");
        rejects([&] { reverseFaces(cube, topology, {}); }, "ORIENTATION_SELECTION");
        rejects([&] { reverseFaces(cube, topology, {999}); }, "ORIENTATION_SELECTION");
        rejects([&] { orientFaces(cube, topology, 999); }, "ORIENTATION_SELECTION");
        auto excessive = cube;
        for (Id id = excessive.nextId; excessive.faces.size() <= 16384; ++id)
            excessive.faces.emplace(id, Face{id, cube.faces.begin()->second.loops});
        rejects([&] { orientFaces(excessive, topology, seed); }, "ORIENTATION_LIMIT");
        for (const auto scale : {Vec3{1, 1, 1}, Vec3{-1.5, .75, 1.2}, Vec3{.01, .01, .01}}) {
            auto placed = cube;
            const auto matrix = Transform::translation({12345, -4567, 321}) *
                                Transform::rotation({2, 1, 3}, .7) * Transform::scaling(scale);
            for (auto &[id, point] : placed.vertices)
                point = matrix.point(point);
            const auto altered = reverseFaces(placed, topology, {cube.faces.rbegin()->first});
            const auto restored = orientFaces(altered.surface, topology, seed);
            check(restored.surface == placed,
                  "Large-origin oblique/reflected/small geometry reorients exactly");
        }
        std::cout << "Face reversal, exhaustive winding repair, holes/cavities, open/disconnected "
                     "sheets, non-manifold/Mobius rejection and budgets passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
