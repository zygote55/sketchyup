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
Id edgeBetween(const Body &body, Vec3 a, Vec3 b) {
    for (const auto &[id, e] : body.topology.edges) {
        auto p = body.surface.vertices.at(e.a), q = body.surface.vertices.at(e.b);
        if ((p == a && q == b) || (p == b && q == a))
            return id;
    }
    throw std::runtime_error("Missing fixture edge");
}
bool connectivity(const Body &a, const Body &b) {
    return a.surface.vertices == b.surface.vertices && a.surface.faces == b.surface.faces &&
           a.surface.wires == b.surface.wires && a.topology.edges == b.topology.edges;
}
int main() {
    try {
        Document doc;
        auto body = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        auto original = doc.bodies().at(body);
        auto face = original->surface.faces.begin()->first;
        auto erased = doc.eraseFace(body, face);
        check(doc.bodies().at(body)->surface.faces.empty() &&
                  doc.bodies().at(body)->surface.wires.size() == 4,
              "Face erase retains boundary wires");
        check(erased.at(body).faces.deleted == std::vector<Id>{face},
              "Face deletion reports retired identity");
        doc.undo();
        check(connectivity(*doc.bodies().at(body), *original), "Face erase undo restores topology");
        doc.redo();
        const auto boundary = doc.bodies().at(body)->topology.edges.begin()->first;
        auto healed = doc.healFace(body, boundary, {0, 0, 0}, {0, 0, 1});
        check(doc.bodies().at(body)->surface.faces.size() == 1 &&
                  doc.bodies().at(body)->surface.wires.empty(),
              "Redrawing closed boundary heals face");
        check(!doc.bodies().at(body)->surface.faces.contains(face) &&
                  healed.at(body).faces.created.size() == 1,
              "Healing creates a fresh face ID");
        auto restored = doc.bodies().at(body);
        doc.undo();
        doc.redo();
        check(connectivity(*doc.bodies().at(body), *restored), "Healing undo/redo is exact");
        // Hole healing fills the selected closed void without changing the surrounding face.
        Document holes;
        auto ring = holes.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                   {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        const auto ringFace = holes.bodies().at(ring)->surface.faces.begin()->first;
        const auto holeEdge = edgeBetween(*holes.bodies().at(ring), {1, 1, 0}, {1, 3, 0});
        holes.healFace(ring, holeEdge, {0, 0, 0}, {0, 0, 1});
        check(holes.bodies().at(ring)->surface.faces.size() == 2 &&
                  holes.bodies().at(ring)->surface.faces.contains(ringFace),
              "Explicit heal fills a hole and preserves surrounding identity");
        Document joined;
        const auto context = joined.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 2, 0}, {0, 2, 0}}});
        joined.insertEdges(context, {0, 0, 0}, {0, 0, 1}, {{{{2, 0, 0}, {2, 2, 0}}}});
        auto divided = joined.bodies().at(context);
        const auto divider = edgeBetween(*divided, {2, 0, 0}, {2, 2, 0});
        const auto merged = joined.eraseEdge(context, divider);
        auto mergedBody = joined.bodies().at(context);
        check(mergedBody->surface.faces.size() == 1 && mergedBody->surface.wires.empty(),
              "Erasing coplanar divider joins faces");
        const auto mergedFace = mergedBody->surface.faces.begin()->first;
        check(std::abs(mergedBody->surface.area(mergedFace) - 8) < 1e-8,
              "Face merge preserves coverage");
        for (const auto &[id, f] : divided->surface.faces)
            check(merged.at(context).faces.descendants.at(id) == std::vector<Id>{mergedFace},
                  "Both source faces map to new joined face");
        joined.undo();
        check(connectivity(*joined.bodies().at(context), *divided),
              "Merge undo restores divided connectivity");
        joined.redo();
        check(connectivity(*joined.bodies().at(context), *mergedBody),
              "Merge redo restores joined topology");
        Document solid;
        auto solidBody = solid.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        solid.extrude(solidBody, solid.bodies().at(solidBody)->surface.faces.begin()->first, 2);
        solid.insertEdges(solidBody, {0, 0, 0}, {0, 0, 1}, {{{{0, 2, 0}, {4, 2, 0}}}});
        solid.eraseEdge(solidBody,
                        edgeBetween(*solid.bodies().at(solidBody), {0, 2, 0}, {4, 2, 0}));
        check(solid.bodies().at(solidBody)->surface.faces.size() == 6,
              "Rejoining subdivided prism restores six faces");
        for (const auto &edge : solid.bodies().at(solidBody)->surface.edges())
            check(edge.faces.size() == 2, "Coplanar union preserves neighbor subdivision vertices");
        // Noncoplanar/nonmanifold edge erase invalidates incident faces, preserving other
        // boundaries.
        Document fan;
        auto radial = std::make_shared<Body>();
        radial->id = 1;
        radial->surface.addFace({{{0, 0, 0}, {4, 0, 0}, {0, 3, 0}}});
        radial->surface.addFace({{{4, 0, 0}, {0, 0, 0}, {0, 0, 3}}});
        radial->surface.addFace({{{0, 0, 0}, {4, 0, 0}, {0, -3, 0}}});
        fan.apply({"Fixture", {{1, nullptr, radial}}}, fan.revision());
        auto fanBefore = fan.bodies().at(1);
        fan.eraseEdge(1, edgeBetween(*fanBefore, {0, 0, 0}, {4, 0, 0}));
        check(fan.bodies().at(1)->surface.faces.empty() &&
                  fan.bodies().at(1)->surface.wires.size() == 6,
              "Nonmanifold erase retains other six boundaries");
        fan.undo();
        check(connectivity(*fan.bodies().at(1), *fanBefore),
              "Nonmanifold erase undo restores incidence");
        // Two neighboring faces with duplicate-position vertex records form a cracked seam.
        Document cleanup;
        auto cracked = std::make_shared<Body>();
        cracked->id = 1;
        cracked->surface.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto second =
            cracked->surface.addFace({{{2, 0, 0}, {4, 0, 0}, {4, 2, 0}, {2, 2, 0}}});
        std::map<Id, Id> copies;
        for (auto &vertex : cracked->surface.faces.at(second).loops[0])
            if (cracked->surface.vertices.at(vertex).x == 2) {
                const auto duplicate = cracked->surface.nextId++;
                cracked->surface.vertices[duplicate] = cracked->surface.vertices.at(vertex);
                copies[duplicate] = vertex;
                vertex = duplicate;
            }
        cleanup.apply({"Fixture", {{1, nullptr, cracked}}}, cleanup.revision());
        const auto unrelated = cleanup.addFace({{{10, 0, 0}, {11, 0, 0}, {10, 1, 0}}});
        auto neighbor = cleanup.bodies().at(unrelated);
        auto crackBefore = cleanup.bodies().at(1);
        auto report = cleanup.cleanup(1);
        auto closed = cleanup.bodies().at(1);
        check(closed->surface.vertices.size() + 2 == crackBefore->surface.vertices.size(),
              "Cleanup merges duplicate-position vertices");
        const auto shared = edgeBetween(*closed, {2, 0, 0}, {2, 2, 0});
        check(closed->topology.adjacency(closed->surface).edgeFaces.at(shared).size() == 2,
              "Cleanup repairs shared edge incidence");
        for (auto [duplicate, canonical] : copies)
            check(report.at(1).vertices.descendants.at(duplicate) == std::vector<Id>{canonical},
                  "Cleanup reports vertex merge lineage");
        check(cleanup.bodies().at(unrelated) == neighbor, "Cleanup never touches another context");
        cleanup.undo();
        check(connectivity(*cleanup.bodies().at(1), *crackBefore),
              "Cleanup undo restores original crack records");
        cleanup.redo();
        check(connectivity(*cleanup.bodies().at(1), *closed),
              "Cleanup redo restores merged records");
        // A warped outline cannot fabricate a face merely because one edge was redrawn.
        Document warped;
        const auto wire = warped.addWire(0, {0, 0, 0}, {2, 0, 0});
        warped.addWire(wire, {2, 0, 0}, {2, 2, .1});
        warped.addWire(wire, {2, 2, .1}, {0, 2, 0});
        warped.addWire(wire, {0, 2, 0}, {0, 0, 0});
        auto wireBefore = warped.bodies().at(wire);
        const auto revision = warped.revision();
        rejects([&] {
            warped.healFace(wire, wireBefore->topology.edges.begin()->first, {0, 0, 0}, {0, 0, 1});
        });
        check(warped.bodies().at(wire) == wireBefore && warped.revision() == revision,
              "Nonplanar healing rejects atomically");
        Document fragile;
        const auto thin = fragile.addFace({{{0, 0, 0},
                                            {2, 0, 0},
                                            {4, 0, 0},
                                            {4, 4, 0},
                                            {2, 4, 0},
                                            {2, tolerance, 0},
                                            {0, 4, 0}}});
        const auto fragileBefore = fragile.bodies().at(thin);
        rejects([&] { fragile.cleanup(thin); });
        check(fragile.bodies().at(thin) == fragileBefore,
              "Cleanup cannot silently collapse a valid face");
        rejects([&] { doc.eraseFace(body, UINT64_MAX); });
        rejects([&] { doc.eraseEdge(body, UINT64_MAX); });
        std::cout << "Erase, coplanar join, explicit healing, scoped cleanup and reversible "
                     "identity maps passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
