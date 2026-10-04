#include "core/model.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <random>
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
int main() {
    try {
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
