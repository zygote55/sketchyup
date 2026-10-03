#include "core/model.hpp"
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &e) {
        check(std::string(e.what()).size() < 4096, "Bounded rejection");
        return;
    }
    throw std::runtime_error("Expected rejection");
}
int main() {
    try {
        std::cout << "{\"linearToleranceMeters\":" << tolerance
                  << ",\"coordinateLimitMeters\":" << coordinateLimit << ",\"cases\":[";
        bool first = true;
        for (auto side : {1e-8, 1e-6, 1e-5, 1e-4, 3e-4, 1e-3, 1.0, 1000.0, 100000.0}) {
            Surface s;
            bool accepted = true;
            try {
                s.addFace({{{0, 0, 0}, {side, 0, 0}, {side, side, 0}, {0, side, 0}}});
            } catch (const std::exception &) {
                accepted = false;
            }
            check(accepted == (side >= 1e-6),
                  "Square scale acceptance changed: update reviewed tolerance contract");
            if (accepted) {
                const auto face = s.faces.begin()->first;
                check(std::abs(s.area(face) - side * side) <= std::max(1e-8, side * side * 1e-8),
                      "Scale area");
                s.extrude(face, 1);
                s.validate();
                for (auto edge : s.edges())
                    check(edge.faces.size() == 2, "Scale prism radial incidence");
            }
            if (!first)
                std::cout << ',';
            first = false;
            std::cout << "{\"squareSideMeters\":" << side
                      << ",\"accepted\":" << (accepted ? "true" : "false") << '}';
        }
        Surface site;
        auto f = site.addFace({{{900000, 900000, 900000},
                                {900010, 900000, 900000},
                                {900010, 900010, 900000},
                                {900000, 900010, 900000}}});
        check(std::abs(site.area(f) - 100) < 1e-8, "Large-origin local projection accuracy");
        site.extrude(f, 2);
        site.validate();
        Surface nearPlane;
        nearPlane.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, tolerance * .5}, {0, 1, 0}}});
        const auto snapshot = nearPlane;
        rejects(
            [&] { nearPlane.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, tolerance * 8}, {0, 1, 0}}}); });
        check(nearPlane == snapshot, "Nonplanar failure preserves exact source");
        rejects([&] { site.addFace({{{coordinateLimit + 1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}); });
        Surface degenerate;
        rejects([&] { degenerate.addFace({{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}}); });
        rejects([&] { degenerate.addFace({{{0, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 0, 0}}}); });
        rejects([&] {
            degenerate.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}},
                                {{0, 1, 0}, {1, 1, 0}, {1, 1.5, 0}}});
        });
        Surface reversed;
        auto face = reversed.addFace({{{0, 0, 0}, {0, 2, 0}, {2, 2, 0}, {2, 0, 0}}});
        auto result = pushPull(reversed, face, -2);
        double volume = 0;
        for (auto t : result.surface.triangles())
            volume += dot(t.a, cross(t.b, t.c)) / 6;
        check(std::abs(volume - 8) < 1e-8, "Reversed negative profile volume");
        Surface radial;
        auto selected = radial.addFace({{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}}});
        radial.addFace({{{2, 0, 0}, {0, 0, 0}, {0, 0, 2}}});
        radial.addFace({{{0, 0, 0}, {2, 0, 0}, {0, -2, 0}}});
        rejects([&] { pushPull(radial, selected, 1); });
        std::cout << "],\"largeOriginMeters\":900000,\"degenerateAndNonmanifoldRejections\":true,"
                     "\"passed\":true}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
