#include "core/model.hpp"
#include <clipper2/clipper.triangulation.h>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace sketchy;
int checks = 0;
void require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected rejection");
}
const std::vector<Vec3> square{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}};
// Runs `body` in a forked child under a 2 GiB address-space cap and a 60 s alarm, so a
// tessellation that allocates without bound fails the check instead of exhausting the machine.
// The child is non-dumpable and turns bad_alloc into an exit code, so a regression leaves no
// core dump. AddressSanitizer reserves far more address space than 2 GiB, so the cap is skipped
// there; the alarm and the triangulation's own work bound still apply.
void capped(const char *message, const std::function<bool()> &body) {
    const pid_t child = fork();
    require(child >= 0, "fork for a capped tessellation check");
    if (child == 0) {
        prctl(PR_SET_DUMPABLE, 0);
#if !defined(__SANITIZE_ADDRESS__)
        const rlimit cap{2ull << 30, 2ull << 30};
        setrlimit(RLIMIT_AS, &cap);
#endif
        alarm(60);
        try {
            _exit(body() ? 0 : 2);
        } catch (const std::bad_alloc &) {
            _exit(3);
        } catch (...) {
            _exit(4);
        }
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            message);
}
// D-042: Clipper2 2.0.1's triangulation tested segment crossings in double, so on faces larger
// than about 10 m (1e-7 m grid) a shared end-point could read as a crossing and edge splitting
// never ended. Each face below hung the unpatched triangulator; all are millimetre-rounded.
bool tessellates(const std::vector<std::vector<Vec3>> &loops, size_t triangles, double area) {
    Surface surface;
    const auto face = surface.addFace(loops);
    surface.validate();
    const auto result = surface.triangulate(face);
    double sum = 0;
    for (const auto &t : result)
        sum += length(cross(t.b - t.a, t.c - t.a)) * .5;
    // The tessellation sits on the 1e-7 m grid, so allow a part-per-million area difference.
    return result.size() == triangles && std::abs(sum - area) <= area * 1e-6 &&
           std::abs(surface.area(face) - area) <= area * 1e-6;
}
void largeFaceTessellation() {
    capped("30 m quad tessellates (bounded)", [] {
        return tessellates(
            {{{17.543, 4.987, 0}, {35.688, -18.903, 0}, {57.817, -8.393, 0}, {41.208, 11.688, 0}}},
            2, 652.939767);
    });
    capped("1000 m quad tessellates (bounded)", [] {
        return tessellates({{{899.167, -270.536, 0},
                             {1820.274, 118.774, 0},
                             {1566.813, 1019.030, 0},
                             {657.910, 471.235, 0}}},
                           2, 867132.135415);
    });
    capped("30 m L-shape tessellates (bounded)", [] {
        return tessellates({{{204.712, 314.298, 0},
                             {208.695, 284.564, 0},
                             {214.850, 285.388, 0},
                             {211.731, 308.669, 0},
                             {225.450, 310.507, 0},
                             {224.585, 316.960, 0}}},
                           4, 276.407761);
    });
    capped("300 m L-shape tessellates (bounded)", [] {
        return tessellates({{{182.932, -634.157, 0},
                             {-106.000, -553.421, 0},
                             {-129.106, -636.110, 0},
                             {5.688, -673.775, 0},
                             {-7.549, -721.145, 0},
                             {146.590, -764.215, 0}}},
                           4, 33628.516466);
    });
    capped("30 m face with a hole tessellates (bounded)", [] {
        return tessellates({{{832.788, -50.505, 0},
                             {844.696, -22.970, 0},
                             {820.753, -12.615, 0},
                             {808.845, -40.151, 0}},
                            {{829.313, -42.817, 0},
                             {815.652, -36.909, 0},
                             {823.945, -17.733, 0},
                             {837.606, -23.641, 0}}},
                           8, 471.6254825);
    });
    capped("1000 m face with a hole tessellates (bounded)", [] {
        return tessellates({{{667.471, -545.200, 0},
                             {1147.244, 332.192, 0},
                             {717.741, 567.052, 0},
                             {237.968, -310.341, 0}},
                            {{746.482, -146.942, 0},
                             {472.229, 3.024, 0},
                             {611.457, 257.639, 0},
                             {885.710, 107.673, 0}}},
                           8, 398813.563978);
    });
    // The work bound itself: with the bounds scaled down, the triangulator stops and the face
    // is refused with the ordinary outline error rather than allocating.
    capped("work-bound overflow refuses the face", [] {
        const double previous = Clipper2Lib::SetTriangulateWorkBoundScale(0.01);
        std::string error;
        try {
            Surface surface;
            surface.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {1, 1, 0}, {1, 4, 0}, {0, 4, 0}}});
        } catch (const std::runtime_error &e) {
            error = e.what();
        }
        Clipper2Lib::SetTriangulateWorkBoundScale(previous);
        Surface surface;
        surface.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {1, 1, 0}, {1, 4, 0}, {0, 4, 0}}});
        return error == "Intersecting or degenerate face outline";
    });
}
int main() {
    try {
        largeFaceTessellation();
        Surface s;
        auto face = s.addFace({square});
        require(std::abs(s.area(face) - 16) < 1e-8, "Square area");
        auto original = s;
        rejects([&] { s.addFace({{{0, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 0, 0}}}); });
        require(s == original, "Failed face atomicity");
        rejects([&] { s.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0.01}, {0, 1, 0}}}); });
        rejects([&] { s.addFace({{{0, 0, 0}, {0, 0, 0}, {1, 1, 0}}}); });
        rejects([&] { s.addFace({{{NAN, 0, 0}, {1, 0, 0}, {1, 1, 0}}}); });
        Surface holes;
        auto h = holes.addFace({square, {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        require(std::abs(holes.area(h) - 12) < 1e-8, "Hole area");
        holes.extrude(h, 3);
        require(holes.faces.size() == 10, "Hollow prism face count");
        for (const auto &e : holes.edges())
            require(e.faces.size() == 2, "Closed prism adjacency");
        double volume = 0;
        for (const auto &t : holes.triangles())
            volume += dot(t.a, cross(t.b, t.c)) / 6;
        require(std::abs(volume - 36) < 1e-7, "Hollow prism oriented volume");
        Surface negative;
        auto nf = negative.addFace({square});
        negative.extrude(nf, -2);
        volume = 0;
        for (const auto &t : negative.triangles())
            volume += dot(t.a, cross(t.b, t.c)) / 6;
        require(std::abs(volume - 32) < 1e-7, "Negative extrusion orientation");
        rejects([&] {
            Surface x;
            x.addFace({square, {{8, 8, 0}, {9, 8, 0}, {9, 9, 0}}});
        });
        rejects([&] {
            Surface x;
            x.addFace({square, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}});
        });
        rejects([&] {
            Surface x;
            x.addFace({square,
                       {{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}},
                       {{2, 2, 0}, {3.5, 2, 0}, {3.5, 3.5, 0}, {2, 3.5, 0}}});
        });
        Surface wire;
        auto a = wire.vertex({0, 0, 0}), b = wire.vertex({1, 0, 0});
        wire.wires.push_back({a, b});
        wire.validate();
        require(wire.edges()[0].faces.empty(), "Loose wire survives");
        // Three incident faces are permitted: the document is not a manifold-only kernel.
        Surface radial;
        radial.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        radial.addFace({{{1, 0, 0}, {0, 0, 0}, {0, 0, 1}}});
        radial.addFace({{{0, 0, 0}, {1, 0, 0}, {0, -1, 0}}});
        radial.validate();
        bool nonmanifold = false;
        for (auto e : radial.edges())
            if (e.faces.size() == 3)
                nonmanifold = true;
        require(nonmanifold, "Radial adjacency");
        Surface tilted;
        auto tf = tilted.addFace({{{0, 0, 0}, {1, 0, 1}, {1, 1, 1}, {0, 1, 0}}});
        require(std::abs(tilted.area(tf) - std::sqrt(2.0)) < 1e-6, "Arbitrary plane area");
        Surface near;
        near.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 1e-9}, {0, 1, 0}}});
        near.validate();
        Document d;
        auto id = d.addFace({square});
        require(d.dirty(), "New edit dirty");
        d.markSaved();
        d.markRecovered();
        require(d.dirty(), "Recovered checkpoint requires explicit save");
        d.markSaved();
        require(!d.dirty(), "Explicit save acknowledges recovered state");
        auto saved = d.bodies().at(id);
        auto rev = d.revision();
        rejects([&] { d.apply({"stale", {{id, saved, nullptr}}}, rev - 1); });
        require(d.revision() == rev, "Stale revision unchanged");
        auto invalid = std::make_shared<Body>(*saved);
        invalid->surface.vertices.begin()->second.x = INFINITY;
        rejects([&] { d.apply({"invalid", {{id, saved, invalid}}}, rev); });
        require(d.bodies().at(id) == saved, "Failed command leaves body intact");
        d.move(id, {1, 2, 3});
        require(d.dirty(), "Move dirty");
        d.undo();
        require(!d.dirty(), "Undo restores saved state");
        require(d.revision() > rev, "Undo increments revision");
        d.redo();
        require(d.dirty(), "Redo dirty");
        d.undo();
        d.paint(id, {.2f, .3f, .4f});
        require(!d.canRedo(), "Edit invalidates redo");
        auto external = std::make_shared<Body>(*d.bodies().at(id));
        d.apply({"freeze", {{id, d.bodies().at(id), external}}}, d.revision());
        external->name = "mutated";
        require(d.bodies().at(id)->name != "mutated", "Caller cannot mutate authoritative state");
        d.erase(id);
        d.undo();
        require(d.bodies().contains(id), "Delete undo");
        d.redo();
        auto next = d.addFace({square});
        require(next > id, "Never reuse IDs");
        std::mt19937 rng(42);
        for (int i = 0; i < 200; ++i) {
            auto before = *d.bodies().at(next);
            d.move(next, {double(rng() % 10) / 100, 0, 0});
            d.undo();
            require(*d.bodies().at(next) == before, "Random move/undo preserves exact topology");
            d.redo();
        }
        require(d.historyBytes() < Document::historyLimit, "Bounded history");
        std::cout << checks << " geometry and document checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
