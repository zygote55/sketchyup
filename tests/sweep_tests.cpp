#include "core/transform.hpp"
#include "geometry/solid.hpp"
#include "geometry/sweep.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message, double eps = 1e-6) {
    check(std::abs(actual - expected) < eps, message);
}
std::vector<Vec3> box(double w, double h) {
    return {{-w / 2, -h / 2, 0}, {w / 2, -h / 2, 0}, {w / 2, h / 2, 0}, {-w / 2, h / 2, 0}};
}
template <class F> void rejects(F fn, const std::string &code) {
    try {
        fn();
    } catch (const SweepError &e) {
        if (e.code() != code)
            throw std::runtime_error("Expected " + code + ", got " + e.code() + ": " + e.what());
        return;
    }
    throw std::runtime_error("Expected " + code);
}
void mapped(const SweepResult &result, size_t vertices, size_t segments, bool closed) {
    check(result.sides.size() == vertices && result.segments.size() == segments,
          "Boundary and path mappings cover all inputs");
    check(result.caps.size() == (closed ? 0u : 2u),
          "Open paths have two caps; closed paths have no seam caps");
    std::set<Id> faces(result.caps.begin(), result.caps.end());
    for (const auto &[edge, sides] : result.sides) {
        check(sides.size() == segments, "Each profile edge maps to one face per path segment");
        faces.insert(sides.begin(), sides.end());
    }
    check(faces.size() == result.surface.faces.size(), "Generated face maps cover output exactly");
    const auto solid = inspectSolid(result.surface, Topology::rebuild(result.surface, {}));
    check(solid.status == "solid" && solid.volume.has_value(),
          "Generated output independently classifies as solid");
}
} // namespace
int main() {
    try {
        Surface profile;
        const auto face = profile.addFace({box(.4, .2)});
        const auto before = profile;
        const auto straight = sweepProfile(profile, face, {{0, 0, 0}, {0, 0, 3}});
        near(straight.volume, .24, "Straight sweep volume equals profile area times length");
        mapped(straight, 4, 1, false);
        check(straight.surface.normal(straight.caps[0]).z < -.99 &&
                  straight.surface.normal(straight.caps[1]).z > .99,
              "Open caps face outward");
        const auto backward = sweepProfile(profile, face, {{0, 0, 0}, {0, 0, -3}});
        near(backward.volume, .24, "Antiparallel first-segment alignment is deterministic");
        const auto elbow = sweepProfile(profile, face, {{0, 0, 0}, {0, 0, 3}, {3, 0, 3}});
        near(elbow.volume, .48, "Mitered elbow conserves the centered profile cross section");
        mapped(elbow, 4, 2, false);
        // Four shared elbow vertices lie on the analytical x+z=3 miter plane.
        size_t corner{};
        for (const auto &[id, p] : elbow.surface.vertices)
            if (std::abs(p.x + p.z - 3) < tolerance)
                ++corner;
        check(corner == 4, "Elbow shares one bisector ring without a seam face");
        std::vector<Vec3> arc;
        double lengthSum{};
        for (int i = 0; i <= 16; ++i) {
            const double angle = std::numbers::pi * .5 * i / 16;
            arc.push_back({3 * (1 - std::cos(angle)), 0, 3 * std::sin(angle)});
            if (i)
                lengthSum += length(arc[i] - arc[i - 1]);
        }
        const auto curved = sweepProfile(profile, face, arc);
        near(curved.volume, .08 * lengthSum,
             "Faceted curved sweep has analytic centered-profile volume");
        mapped(curved, 4, 16, false);
        const auto loop =
            sweepProfile(profile, face, {{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}, true);
        near(loop.volume, .08 * 14, "Closed rectangle has expected volume and no seam caps");
        mapped(loop, 4, 4, true);
        const auto repeated = sweepProfile(
            profile, face, {{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}, {0, 0, 0}}, true);
        check(loop.surface == repeated.surface,
              "Explicit closing station is canonicalized exactly");
        Surface ring;
        const auto hole = ring.addFace({box(.4, .4), box(.2, .2)});
        const auto tube = sweepProfile(ring, hole, {{0, 0, 0}, {0, 0, 2}, {2, 0, 2}});
        near(tube.volume, .12 * 4, "Open holed profile forms a hollow elbow");
        mapped(tube, 8, 2, false);
        auto reversed = box(.4, .2);
        std::reverse(reversed.begin(), reversed.end());
        Surface rev;
        const auto rf = rev.addFace({reversed});
        near(sweepProfile(rev, rf, {{0, 0, 0}, {0, 0, 3}, {3, 0, 3}}).volume, .48,
             "Reversed profile winding still produces outward solid");
        const auto spatial =
            sweepProfile(profile, face, {{0, 0, 0}, {0, 0, 3}, {3, 0, 3}, {3, 3, 3}, {5, 3, 5}});
        near(spatial.volume, .08 * (9 + std::sqrt(8.)),
             "Open spatial path transports cross section without scaling");
        mapped(spatial, 4, 4, false);
        for (int i = 1; i <= 12; ++i) {
            const auto transform = Transform::translation({800000, -700000, 600000}) *
                                   Transform::rotation({1, 2, 3}, i * .173);
            auto rotated = profile;
            for (auto &[id, point] : rotated.vertices)
                point = transform.point(point);
            std::vector<Vec3> path;
            for (auto point : std::vector<Vec3>{{0, 0, 0}, {0, 0, 3}, {3, 0, 3}})
                path.push_back(transform.point(point));
            const auto result = sweepProfile(rotated, face, path);
            near(result.volume, .48, "Oblique large-origin elbow preserves volume", 2e-6);
            check(result.surface.vertices.size() == elbow.surface.vertices.size(),
                  "Rigid transformation preserves generated vertex count");
            for (const auto &[id, point] : elbow.surface.vertices)
                check(length(result.surface.vertices.at(id) - transform.point(point)) < 2e-8,
                      "Transported corners commute with rigid world transforms");
        }
        Surface tiny;
        const auto tinyFace = tiny.addFace({box(.0004, .0002)});
        near(sweepProfile(tiny, tinyFace, {{0, 0, 0}, {0, 0, .003}}).volume, 2.4e-10,
             "Submillimeter profile retains its cross section", 1e-13);
        Surface concave;
        const auto concaveFace = concave.addFace(
            {{{0, 0, 0}, {.4, 0, 0}, {.4, .1, 0}, {.1, .1, 0}, {.1, .4, 0}, {0, .4, 0}}});
        near(sweepProfile(concave, concaveFace, {{0, 0, 0}, {0, 0, 3}}).volume, .21,
             "Concave profile is capped without filling its missing corner");
        rejects(
            [&] {
                sweepProfile(profile, face,
                             {{0, 0, 0}, {4, 0, 0}, {4, 4, 1}, {0, 4, 0}, {-1, 2, 3}}, true);
            },
            "SWEEP_TWIST");
        rejects([&] { sweepProfile(profile, face, {{0, 0, 0}, {0, 0, 3}, {0, 0, 2}}); },
                "SWEEP_SHARP_TURN");
        rejects(
            [&] {
                sweepProfile(profile, face,
                             {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}, {0, 2, 0}, {6, 2, 0}});
            },
            "SWEEP_SELF_INTERSECTION");
        rejects([&] { sweepProfile(profile, face, {{0, 0, 0}, {0, 0, .05}, {.05, 0, .05}}); },
                "SWEEP_SELF_INTERSECTION");
        rejects([&] { sweepProfile(profile, face, {{0, 0, 0}, {0, 0, 0}}); }, "SWEEP_INVALID_PATH");
        rejects([&] { sweepProfile(profile, face, {{0, 0, 1}, {0, 0, 3}}); },
                "SWEEP_PROFILE_ALIGNMENT");
        rejects(
            [&] {
                sweepProfile(profile, face,
                             {{0, 0, 0}, {std::numeric_limits<double>::infinity(), 0, 0}});
            },
            "SWEEP_INVALID_PATH");
        rejects([&] { sweepProfile(profile, 999, {{0, 0, 0}, {0, 0, 3}}); },
                "SWEEP_INVALID_PROFILE");
        auto invalid = profile;
        invalid.vertices.at(invalid.faces.at(face).loops[0][0]).z = .01;
        rejects([&] { sweepProfile(invalid, face, {{0, 0, 0}, {0, 0, 3}}); },
                "SWEEP_INVALID_PROFILE");
        rejects([&] { sweepProfile(profile, face, {{0, 0, 0}, {0, 1e6, 0}, {1e6, 1e6, 0}}); },
                "SWEEP_RANGE");
        rejects(
            [&] { sweepProfile(ring, hole, {{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}, true); },
            "SWEEP_CLOSED_HOLES");
        std::vector<Vec3> many(130);
        rejects([&] { sweepProfile(profile, face, many); }, "SWEEP_LIMIT");
        check(profile == before,
              "Sweep success and failure never change source geometry or allocator");
        std::cout << "Sweep straight/curved/spatial/closed paths, holes, mappings, exact volume "
                     "and classified rejection passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
