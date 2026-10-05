#include "core/transform.hpp"
#include "geometry/solid.hpp"
#include <algorithm>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
SolidReport inspect(const Surface &surface) {
    surface.validate();
    return inspectSolid(surface, Topology::rebuild(surface, {}));
}
Surface prism(const std::vector<std::vector<Vec3>> &loops, double depth) {
    Surface surface;
    const auto face = surface.addFace(loops);
    surface.extrude(face, depth);
    return surface;
}
void volume(const Surface &surface, double expected, const char *message) {
    const auto result = inspect(surface);
    check(result.volume &&
              std::abs(*result.volume - expected) <= tolerance * std::max(1., expected),
          std::string(message) + ": " + result.status);
}
void append(Surface &target, const Surface &source, Vec3 delta) {
    for (const auto &[id, face] : source.faces) {
        std::vector<std::vector<Vec3>> loops;
        for (const auto &loop : face.loops) {
            loops.emplace_back();
            for (auto vertex : loop)
                loops.back().push_back(source.vertices.at(vertex) + delta);
        }
        target.addFace(loops);
    }
}
int main() {
    try {
        const auto box = prism({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}}, 4);
        volume(box, 24, "Box volume follows a closed oriented shell");
        auto mirror = box;
        const auto matrix = Transform::scaling({-2, 3, .5});
        for (auto &[id, p] : mirror.vertices)
            p = matrix.point(p);
        volume(mirror, 72, "Mirrored nonuniform geometry has positive volume");
        auto reversed = box;
        for (auto &[id, face] : reversed.faces)
            for (auto &loop : face.loops)
                std::reverse(loop.begin(), loop.end());
        volume(reversed, 24, "Entirely inward oriented shell has the same volume");
        auto far = box;
        far.translate({999990, 999990, 999990});
        volume(far, 24, "Translation near coordinate limits does not lose volume");
        const auto concave =
            prism({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}}}, 2);
        volume(concave, 6, "Concave single-shell volume");
        const auto hole = prism({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                 {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}}},
                                2);
        volume(hole, 30, "A through hole retains a valid connected solid shell");
        for (const auto dimensions :
             {std::pair{1., 4.}, std::pair{.005, .01}, std::pair{1., 10000.}}) {
            std::vector<Vec3> ring;
            for (unsigned i = 0; i < 48; ++i) {
                const auto angle = 2 * std::numbers::pi * i / 48;
                ring.push_back(
                    {dimensions.first * std::sin(angle), -dimensions.first * std::cos(angle), 0});
            }
            const auto result = inspect(prism({ring}, dimensions.second));
            const auto radius = dimensions.first, height = dimensions.second;
            const auto capArea = 24 * radius * radius * std::sin(std::numbers::pi / 24);
            const auto area = 2 * capArea + 96 * radius * std::sin(std::numbers::pi / 48) * height;
            // Triangulation rounds projected coordinates at the geometry tolerance.
            // Its volume error scales with surface area, not with volume alone.
            check(result.volume &&
                      std::abs(*result.volume - capArea * height) <= 4 * tolerance * area,
                  "Faceted cylinders tolerate rounded shared-edge contacts: " + result.status);
        }
        auto open = box;
        Surface coplanarFold;
        const Vec3 a{0, 0, 0}, b{2, 0, 0}, c{0, 2, 0}, d{.5, .5, 0};
        for (const auto &face : std::vector<std::vector<Vec3>>{{a, c, b}, {a, b, d}, {b, c, d}, {c, a, d}})
            coplanarFold.addFace({face});
        check(inspect(coplanarFold).status == "self_intersection",
              "Coplanar overlap outside shared-edge tubes remains a self-intersection");
        open.faces.erase(open.faces.begin());
        check(inspect(open).status == "open_boundary" && !inspect(open).volume,
              "Open surface has no volume");
        auto winding = box;
        auto &loop = winding.faces.begin()->second.loops.front();
        std::reverse(loop.begin(), loop.end());
        check(inspect(winding).status == "inconsistent_winding", "Mixed winding rejects volume");
        auto loose = box;
        loose.vertex({8, 8, 8});
        check(inspect(loose).status == "loose_geometry",
              "Loose vertices prevent solid classification");
        auto disconnected = box;
        append(disconnected, box, {8, 0, 0});
        check(inspect(disconnected).status == "multiple_shells" && !inspect(disconnected).volume,
              "Multiple shells require further analysis rather than an assumed sum");
        auto touching = box;
        append(touching, box, {2, 3, 4});
        check(inspect(touching).status == "non_manifold",
              "Point-contact shells have disconnected vertex fans");
        Surface folded;
        const Vec3 top{2, 0, -.5}, bottom{0, 0, -1};
        const std::array<Vec3, 4> ring{{{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}}};
        for (size_t i = 0; i < 4; ++i) {
            folded.addFace({{top, ring[i], ring[(i + 1) % 4]}});
            folded.addFace({{bottom, ring[(i + 1) % 4], ring[i]}});
        }
        const auto foldedResult = inspect(folded);
        check(foldedResult.status == "self_intersection" && !foldedResult.volume,
              "Crossing closed triangular shell rejects volume: " + foldedResult.status);
        Surface highValence;
        constexpr size_t segments = 700;
        for (size_t i = 0; i < segments; ++i) {
            const auto a = double(i) * 2 * std::numbers::pi / segments;
            const auto b = double(i + 1) * 2 * std::numbers::pi / segments;
            const Vec3 first{std::cos(a), std::sin(a), 0}, second{std::cos(b), std::sin(b), 0};
            highValence.addFace({{Vec3{0, 0, 1}, first, second}});
            highValence.addFace({{Vec3{0, 0, -1}, second, first}});
        }
        const auto bounded = inspect(highValence);
        check(bounded.status == "analysis_limit" && !bounded.volume,
              "High-valence analysis fails boundedly without claiming a volume");
        std::cout << "Closed shell classification, affine volume, holes, winding, vertex fans and "
                     "intersections passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
