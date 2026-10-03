#include "core/model.hpp"
#include "geometry/arrangement.hpp"
#include <iostream>
#include <random>
#include <set>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void adjacency(const Body &body) {
    const auto &surface = body.surface;
    const auto &topology = body.topology;
    topology.validate(surface);
    const auto records = topology.adjacency(surface);
    size_t expected = 0, actual = 0;
    for (const auto &[id, face] : surface.faces)
        for (size_t l = 0; l < face.loops.size(); ++l) {
            const auto &loop = face.loops[l];
            expected += loop.size();
            const auto &oriented = records.faceLoops.at(id).at(l);
            check(oriented.size() == loop.size(), "Oriented loop length");
            for (size_t i = 0; i < loop.size(); ++i) {
                const auto &edge = topology.edges.at(oriented[i].edge);
                check((oriented[i].reversed ? edge.b : edge.a) == loop[i] &&
                          (oriented[i].reversed ? edge.a : edge.b) == loop[(i + 1) % loop.size()],
                      "Oriented loop endpoints");
            }
        }
    for (const auto &[id, incidence] : records.edgeFaces)
        actual += incidence.size();
    check(actual == expected, "Radial incidence covers every loop segment");
    for (const auto &[id, edges] : records.vertexEdges)
        for (auto edge : edges)
            check(topology.edges.at(edge).a == id || topology.edges.at(edge).b == id,
                  "Vertex incidence");
}
int main() {
    try {
        Document doc;
        auto body = std::make_shared<Body>();
        body->id = doc.nextId();
        body->surface.addFace({{{0, 0, 0}, {4, 0, 0}, {0, 3, 0}}});
        body->surface.addFace({{{4, 0, 0}, {0, 0, 0}, {0, 0, 3}}});
        body->surface.addFace({{{0, 0, 0}, {4, 0, 0}, {0, -3, 0}}});
        doc.apply({"Nonmanifold fixture", {{body->id, nullptr, body}}}, doc.revision());
        auto before = doc.bodies().at(body->id);
        adjacency(*before);
        Id shared = 0;
        for (const auto &[id, faces] : before->topology.adjacency(before->surface).edgeFaces)
            if (faces.size() == 3)
                shared = id;
        check(shared != 0, "Three radial faces share one persistent edge");
        auto changed = std::make_shared<Body>(*before);
        const auto vertex = splitEdge(changed->surface, before->topology.edges.at(shared), .4);
        const auto report =
            doc.apply({"Split shared edge", {{body->id, before, changed}}}, doc.revision());
        auto after = doc.bodies().at(body->id);
        adjacency(*after);
        check(report.at(body->id).edges.deleted == std::vector<Id>{shared},
              "Split retires original edge");
        check(report.at(body->id).edges.descendants.at(shared).size() == 2,
              "Split maps edge to both children");
        check(report.at(body->id).faces.modified.size() == 3 &&
                  report.at(body->id).vertices.created == std::vector<Id>{vertex},
              "Split propagates every incident face");
        for (auto edge : report.at(body->id).edges.descendants.at(shared))
            check(after->topology.adjacency(after->surface).edgeFaces.at(edge).size() == 3,
                  "Split preserves nonmanifold radial connectivity");
        const auto edgeFloor = after->topology.nextId;
        doc.undo();
        check(doc.bodies().at(body->id)->topology.edges == before->topology.edges,
              "Undo restores exact edge identities");
        check(doc.bodies().at(body->id)->topology.nextId == edgeFloor,
              "Undo retains edge allocator floor");
        doc.redo();
        check(*doc.bodies().at(body->id) == *after, "Redo restores exact topology");
        doc.undo();
        doc.splitEdge(body->id, shared, .6);
        for (const auto &[id, edge] : doc.bodies().at(body->id)->topology.edges)
            check(before->topology.edges.contains(id) || id >= edgeFloor,
                  "Branched split never reuses retired edge IDs");
        auto stable = doc.bodies().at(body->id);
        rejects([&] { doc.splitEdge(body->id, shared, .5); });
        rejects([&] { doc.splitEdge(body->id, stable->topology.edges.begin()->first, 0); });
        check(doc.bodies().at(body->id) == stable, "Invalid split leaves document intact");
        // Wire-only editing contexts support arbitrary open 3D segments.
        Document wires;
        const auto context = wires.addWire(0, {0, 0, 0}, {2, 2, 2});
        auto wire = wires.bodies().at(context);
        check(wire->surface.faces.empty() && wire->topology.edges.size() == 1, "Wire-only context");
        wires.splitEdge(context, wire->topology.edges.begin()->first, .5);
        check(wires.bodies().at(context)->surface.wires.size() == 2, "Loose wire split");
        adjacency(*wires.bodies().at(context));
        rejects([&] { wires.addWire(context, {0, 0, 0}, {1, 1, 1}); });
        // Face lineage is explicit; mappings cannot point outside committed records.
        Document partition;
        const auto faceBody = partition.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        auto original = partition.bodies().at(faceBody);
        const auto face = original->surface.faces.begin()->first;
        auto split = partitionFace(original->surface, face, {2, 0, 0}, {1, 0, 0});
        auto divided = std::make_shared<Body>(*original);
        divided->surface = split.surface;
        auto mapping =
            partition.apply({"Partition", {{faceBody, original, divided, split.descendants}}},
                            partition.revision());
        check(mapping.at(faceBody).faces.descendants.at(face) == split.descendants.at(face),
              "Explicit face split map survives commit");
        adjacency(*partition.bodies().at(faceBody));
        auto currentPartition = partition.bodies().at(faceBody);
        rejects([&] {
            partition.apply(
                {"Invalid lineage",
                 {{faceBody, currentPartition, currentPartition, {{face, {UINT64_MAX}}}}}},
                partition.revision());
        });
        check(partition.bodies().at(faceBody) == currentPartition,
              "Invalid lineage rejects before publication");
        // Large replacement matching is explicitly bounded, rather than quadratic without limit.
        Surface many;
        for (int i = 0; i < 1001; ++i)
            many.wires.push_back({many.vertex({0, double(i), 0}), many.vertex({1, double(i), 0})});
        const auto indexed = Topology::rebuild(many, {});
        auto replacement = many;
        replacement.wires.clear();
        for (int i = 0; i < 1001; ++i)
            replacement.wires.push_back({replacement.vertex({.25, double(i), 0}),
                                         replacement.vertex({1.25, double(i), 0})});
        const auto replaced = Topology::rebuild(replacement, indexed);
        rejects([&] { compareTopology(many, indexed, replacement, replaced); });
        // Seeded split/undo/redo sequences assert invariants and allocator monotonicity.
        std::mt19937 random(0x1500);
        for (int i = 0; i < 120; ++i) {
            auto current = wires.bodies().at(context);
            auto edge = current->topology.edges.begin();
            std::advance(edge, random() % current->topology.edges.size());
            const auto previousFloor = current->topology.nextId;
            wires.splitEdge(context, edge->first, .3 + double(random() % 40) / 100);
            adjacency(*wires.bodies().at(context));
            if (i % 3 == 0) {
                wires.undo();
                adjacency(*wires.bodies().at(context));
                wires.redo();
            }
            check(wires.bodies().at(context)->topology.nextId >= previousFloor,
                  "Monotonic edge allocation");
        }
        std::cout
            << "Persistent edge records, radial adjacency, lineage and 120 seeded edits passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
