#include "core/model.hpp"
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
double volume(const Surface &s) {
    double result = 0;
    for (auto t : s.triangles())
        result += dot(t.a, cross(t.b, t.c)) / 6;
    return result;
}
void closed(const Surface &s, double expected) {
    s.validate();
    for (auto edge : s.edges())
        check(edge.faces.size() == 2, "Closed radial incidence");
    check(std::abs(volume(s) - expected) < 1e-6, "Expected oriented volume");
}
Id upward(const Surface &s) {
    for (auto [id, f] : s.faces)
        if (s.normal(id).z > .99)
            return id;
    throw std::runtime_error("No upward face");
}
Document box() {
    Document d;
    auto b = d.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
    d.extrude(b, d.bodies().at(b)->surface.faces.begin()->first, 2);
    return d;
}
Id patch(Document &d) {
    d.insertEdges(1, {0, 0, 2}, {0, 0, 1},
                  {{{{1, 1, 2}, {3, 1, 2}}},
                   {{{3, 1, 2}, {3, 3, 2}}},
                   {{{3, 3, 2}, {1, 3, 2}}},
                   {{{1, 3, 2}, {1, 1, 2}}}});
    for (auto [id, f] : d.bodies().at(1)->surface.faces)
        if (d.bodies().at(1)->surface.normal(id).z > .99 &&
            std::abs(d.bodies().at(1)->surface.area(id) - 4) < 1e-8)
            return id;
    throw std::runtime_error("No patch face");
}
bool same(const Body &a, const Body &b) {
    return a.surface.vertices == b.surface.vertices && a.surface.faces == b.surface.faces &&
           a.surface.wires == b.surface.wires && a.topology.edges == b.topology.edges;
}
int main() {
    try {
        auto d = box();
        auto before = d.bodies().at(1);
        auto top = upward(before->surface);
        d.pushPull(1, top, 3);
        closed(d.bodies().at(1)->surface, 80);
        check(d.bodies().at(1)->surface.faces == before->surface.faces,
              "Prism cap retains all face IDs and loops");
        d.pushPull(1, top, -4);
        closed(d.bodies().at(1)->surface, 16);
        auto shortBox = d.bodies().at(1);
        rejects([&] { d.pushPull(1, top, -2); });
        check(d.bodies().at(1) == shortBox, "Crossing opposite face rejects atomically");
        rejects([&] { d.pushPull(1, top, -1); });
        check(d.bodies().at(1) == shortBox, "Collapsing complete prism rejects atomically");
        d.undo();
        d.undo();
        check(same(*d.bodies().at(1), *before), "Cap undo restores exact topology");
        d.redo();
        closed(d.bodies().at(1)->surface, 80);
        for (double distance : {2.0, -2.0}) {
            Document profile;
            auto b =
                profile.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {2, 1, 0}, {2, 4, 0}, {0, 4, 0}},
                                 {{.5, .5, 0}, {.5, 1.5, 0}, {1.5, 1.5, 0}, {1.5, .5, 0}}});
            auto face = profile.bodies().at(b)->surface.faces.begin()->first;
            auto area = profile.bodies().at(b)->surface.area(face);
            auto report = profile.pushPull(b, face, distance);
            closed(profile.bodies().at(b)->surface, area * 2);
            check(report.at(b).faces.descendants.at(face).size() == 2,
                  "Isolated face maps to base and cap");
            auto cap = upward(profile.bodies().at(b)->surface);
            profile.pushPull(b, cap, 1);
            closed(profile.bodies().at(b)->surface, area * 3);
        }
        auto outward = box();
        auto selected = patch(outward);
        outward.pushPull(1, selected, 1);
        closed(outward.bodies().at(1)->surface, 36);
        auto recess = box();
        selected = patch(recess);
        auto recessBefore = recess.bodies().at(1);
        recess.pushPull(1, selected, -1);
        closed(recess.bodies().at(1)->surface, 28);
        recess.undo();
        check(same(*recess.bodies().at(1), *recessBefore), "Recess undo restores selected region");
        auto wall = box();
        selected = patch(wall);
        auto wallBefore = wall.bodies().at(1);
        auto cut = wall.pushPull(1, selected, -2);
        auto opening = wall.bodies().at(1);
        closed(opening->surface, 24);
        check(opening->surface.faces.size() == 10,
              "Opening has outer walls, tunnel walls and two holed caps");
        check(cut.at(1).faces.descendants.at(selected).empty(), "Opening retires selected cap");
        wall.undo();
        check(same(*wall.bodies().at(1), *wallBefore), "Opening undo restores exact topology");
        wall.redo();
        check(same(*wall.bodies().at(1), *opening), "Opening redo restores exact identities");
        // A holed patch sweeps an annulus, leaving the center pillar intact.
        auto annulus = box();
        auto outer = patch(annulus);
        annulus.insertEdges(1, {0, 0, 2}, {0, 0, 1},
                            {{{{1.5, 1.5, 2}, {2.5, 1.5, 2}}},
                             {{{2.5, 1.5, 2}, {2.5, 2.5, 2}}},
                             {{{2.5, 2.5, 2}, {1.5, 2.5, 2}}},
                             {{{1.5, 2.5, 2}, {1.5, 1.5, 2}}}});
        for (const auto &[id, f] : annulus.bodies().at(1)->surface.faces)
            if (annulus.bodies().at(1)->surface.normal(id).z > .99 &&
                std::abs(annulus.bodies().at(1)->surface.area(id) - 3) < 1e-8)
                outer = id;
        annulus.pushPull(1, outer, -2);
        closed(annulus.bodies().at(1)->surface, 26);
        Document separate;
        auto part = std::make_shared<Body>();
        part->id = 1;
        auto isolated = part->surface.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        part->surface.addFace({{{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}}});
        separate.apply({"Two islands", {{1, nullptr, part}}}, separate.revision());
        separate.pushPull(1, isolated, 2);
        check(separate.bodies().at(1)->surface.faces.size() == 7,
              "Disconnected isolated face keeps its base");
        auto invalid = box();
        selected = patch(invalid);
        auto safe = invalid.bodies().at(1);
        auto revision = invalid.revision();
        rejects([&] { invalid.pushPull(1, selected, -3); });
        rejects([&] { invalid.pushPull(1, selected, NAN); });
        rejects([&] { invalid.pushPull(1, selected, 1e7); });
        check(invalid.bodies().at(1) == safe && invalid.revision() == revision,
              "Invalid sweeps leave document unchanged");
        auto obstacle = std::make_shared<Body>(*safe);
        obstacle->surface.addFace({{{1, 1, 1}, {3, 1, 1.5}, {3, 3, 1.5}, {1, 3, 1}}});
        invalid.apply({"Obstacle", {{1, safe, obstacle}}}, invalid.revision());
        safe = invalid.bodies().at(1);
        rejects([&] { invalid.pushPull(1, selected, -2); });
        check(invalid.bodies().at(1) == safe, "Sloped obstruction rejects atomically");
        std::cout << "Push/pull prism, concave/holed profiles, recesses, openings, collisions and "
                     "identity tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
