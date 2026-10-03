#include "geometry/planar.hpp"
#include <algorithm>
#include <iostream>
#include <random>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<std::array<Vec3, 2>> outline(std::vector<Vec3> points) {
    std::vector<std::array<Vec3, 2>> result;
    for (size_t i = 0; i < points.size(); ++i)
        result.push_back({points[i], points[(i + 1) % points.size()]});
    return result;
}
double area(const Surface &surface) {
    double sum = 0;
    for (const auto &[id, f] : surface.faces)
        sum += surface.area(id);
    return sum;
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const PlanarError &error) {
        check(error.code() == code, "Structured diagnostic code");
        return;
    }
    throw std::runtime_error("Expected planar rejection");
}
int main() {
    try {
        const auto square = outline({{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}});
        Surface empty;
        auto formed = insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1}, square);
        check(formed.surface.faces.size() == 1 && formed.surface.wires.empty() &&
                  std::abs(area(formed.surface) - 16) < 1e-8,
              "Closed outline forms face");
        auto coincident = insertPlanarEdges(formed.surface, {0, 0, 0}, {0, 0, 1}, square);
        check(coincident.surface == formed.surface, "Coincident redrawing is exact no-op");
        const auto originalFace = formed.surface.faces.begin()->first;
        auto crossed = insertPlanarEdges(formed.surface, {0, 0, 0}, {0, 0, 1},
                                         {{{{-1, 2, 0}, {5, 2, 0}}}, {{{2, -1, 0}, {2, 5, 0}}}});
        check(crossed.surface.faces.size() == 4 && crossed.faces.at(originalFace).size() == 4 &&
                  std::abs(area(crossed.surface) - 16) < 1e-8,
              "Crossing segments split into four faces");
        check(crossed.surface.wires.size() == 4, "Exterior open tails remain wires");
        check(!crossed.surface.faces.contains(originalFace),
              "Subdivision retires original face ID");
        auto partial =
            insertPlanarEdges(formed.surface, {0, 0, 0}, {0, 0, 1}, {{{{0, 2, 0}, {2, 2, 0}}}});
        check(partial.surface.faces.size() == 1 && partial.surface.faces.contains(originalFace) &&
                  partial.surface.wires.size() == 1,
              "Interior dangling edge does not fabricate face");
        check(partial.surface.faces.at(originalFace).loops[0].size() == 5,
              "Dangling edge splits touched boundary");
        auto nestedEdges = square;
        auto inner = outline({{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}});
        nestedEdges.insert(nestedEdges.end(), inner.begin(), inner.end());
        auto nested = insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1}, nestedEdges);
        check(nested.surface.faces.size() == 2 && std::abs(area(nested.surface) - 16) < 1e-8,
              "Nested cycles form inner face and outer holed face");
        int holes = 0;
        for (const auto &[id, f] : nested.surface.faces)
            holes += f.loops.size() - 1;
        check(holes == 1, "Nested containment becomes oriented hole");
        Surface ring;
        auto ringId = ring.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                    {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        auto cut = insertPlanarEdges(ring, {0, 0, 0}, {0, 0, 1}, {{{{-1, 2, 0}, {5, 2, 0}}}});
        check(cut.surface.faces.size() == 2 && cut.faces.at(ringId).size() == 2 &&
                  std::abs(area(cut.surface) - 12) < 1e-8,
              "Cut preserves existing hole coverage");
        auto overlap = insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1},
                                         {{{{0, 0, 0}, {4, 0, 0}}}, {{{3, 0, 0}, {1, 0, 0}}}});
        check(overlap.surface.faces.empty() && overlap.surface.wires.size() == 3,
              "Reversed partial overlap coalesces without duplicate edges");
        auto complete = insertPlanarEdges(
            overlap.surface, {0, 0, 0}, {0, 0, 1},
            {{{{4, 0, 0}, {4, 2, 0}}}, {{{4, 2, 0}, {0, 2, 0}}}, {{{0, 2, 0}, {0, 0, 0}}}});
        check(complete.surface.faces.size() == 1 && std::abs(area(complete.surface) - 8) < 1e-8,
              "Existing split wires close into face");
        auto near =
            insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1},
                              outline({{0, 0, 1e-9}, {2, 0, -1e-9}, {2, 2, 1e-9}, {0, 2, 0}}));
        check(near.surface.faces.size() == 1 && std::abs(area(near.surface) - 4) < 1e-8,
              "Nearly planar input snaps within tolerance");
        auto tilted = insertPlanarEdges(empty, {0, 0, 0}, {0, 1, 0},
                                        outline({{0, 0, 0}, {2, 0, 0}, {2, 0, 3}, {0, 0, 3}}));
        check(tilted.surface.faces.size() == 1 && std::abs(area(tilted.surface) - 6) < 1e-8,
              "Arbitrary editing plane");
        // A split on a prism's base boundary propagates to adjacent noncoplanar sides.
        Surface prism = formed.surface;
        prism.extrude(originalFace, 2);
        auto prismCut = insertPlanarEdges(prism, {0, 0, 0}, {0, 0, 1}, {{{{0, 2, 0}, {4, 2, 0}}}});
        check(prismCut.surface.faces.size() == 7, "Base face subdivides inside solid shell");
        for (const auto &edge : prismCut.surface.edges())
            check(edge.faces.size() == 2, "Side loops receive boundary splits without T junctions");
        // Closed old wire loops disconnected from the edit remain intentionally unfilled.
        Surface untouched;
        for (auto edge : square)
            untouched.wires.push_back({untouched.vertex(edge[0]), untouched.vertex(edge[1])});
        auto distant =
            insertPlanarEdges(untouched, {0, 0, 0}, {0, 0, 1}, {{{{10, 0, 0}, {11, 0, 0}}}});
        check(distant.surface.faces.empty() && distant.surface.wires.size() == 5,
              "Unrelated closed wire network is untouched");
        rejects(
            [&] {
                insertPlanarEdges(formed.surface, {0, 0, 0}, {0, 0, 1},
                                  {{{{0, 0, 0}, {1, 1, .01}}}});
            },
            "NON_PLANAR_INPUT");
        Surface multipleHoles;
        multipleHoles.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 8, 0}, {0, 8, 0}},
                               {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}},
                               {{6, 1, 0}, {6, 3, 0}, {8, 3, 0}, {8, 1, 0}}});
        auto multi =
            insertPlanarEdges(multipleHoles, {0, 0, 0}, {0, 0, 1}, {{{{0, 5, 0}, {10, 5, 0}}}});
        check(multi.surface.faces.size() == 2 && std::abs(area(multi.surface) - 72) < 1e-8,
              "Multiple holes survive subdivision");
        auto site = insertPlanarEdges(empty, {900000, 900000, 0}, {0, 0, 1},
                                      outline({{900000, 900000, 0},
                                               {900004, 900000, 0},
                                               {900004, 900004, 0},
                                               {900000, 900004, 0}}));
        check(std::abs(area(site.surface) - 16) < 1e-8, "Large site coordinates use a local plane");
        auto linked = nestedEdges;
        linked.push_back({Vec3{0, 2, 0}, Vec3{1, 2, 0}});
        auto bridge = insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1}, linked);
        check(bridge.surface.faces.size() == 2 && bridge.surface.wires.size() == 1 &&
                  std::abs(area(bridge.surface) - 16) < 1e-8,
              "Bridge between nested cycles remains a wire");
        std::mt19937 random(0x1600);
        auto shuffled = square;
        shuffled.push_back({Vec3{0, 2, 0}, Vec3{4, 2, 0}});
        shuffled.push_back({Vec3{2, 0, 0}, Vec3{2, 4, 0}});
        for (int i = 0; i < 24; ++i) {
            std::shuffle(shuffled.begin(), shuffled.end(), random);
            for (auto &edge : shuffled)
                if (random() % 2)
                    std::swap(edge[0], edge[1]);
            auto arrangement = insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1}, shuffled);
            check(arrangement.surface.faces.size() == 4 &&
                      std::abs(area(arrangement.surface) - 16) < 1e-8,
                  "Input order and segment direction preserve arrangement coverage");
        }
        rejects([&] { insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1}, {{{{0, 0, 0}, {0, 0, 0}}}}); },
                "DEGENERATE_EDGE");
        rejects(
            [&] {
                insertPlanarEdges(empty, {0, 0, 0}, {0, 0, 1},
                                  {{{{0, 0, 0}, {100, 0, 0}}}, {{{0, -1e-8, 0}, {100, 1e-8, 0}}}});
            },
            "UNSTABLE_INTERSECTION");
        check(formed.surface.faces.size() == 1 && formed.surface.faces.contains(originalFace),
              "Source immutable after rejected edits");
        std::cout << "Finite planar arrangements: closed, nested, crossing, overlap, holes, tilted "
                     "and shared boundaries passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
