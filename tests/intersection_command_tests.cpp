#include "automation/commands.hpp"
#include "automation/session.hpp"
#include "core/appearance.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/intersection_edit.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, const char *message) { check(std::abs(a - b) < 1e-6, message); }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(Id body, Id face, QString mode = "context", Id context = 0) {
    return {{"command", "geometry.intersect"},
            {"context", QString::number(context)},
            {"mode", mode},
            {"entities", QJsonArray{QJsonObject{{"body", QString::number(body)},
                                                {"face", QString::number(face)}}}}};
}
Id horizontal(Document &doc, bool hole = false) {
    std::vector<std::vector<Vec3>> loops{{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}};
    if (hole)
        loops.push_back({{.5, 1, 0}, {2.5, 1, 0}, {2.5, 2, 0}, {.5, 2, 0}});
    return doc.addFace(loops);
}
Id vertical(Document &doc, Transform frame = {}) {
    std::vector<Vec3> points{{1, -1, -1}, {1, 4, -1}, {1, 4, 1}, {1, -1, 1}};
    for (auto &p : points)
        p = frame.point(p);
    return doc.addFace({points});
}
Id firstFace(const Document &doc, Id body) {
    return doc.bodies().at(body)->surface.faces.begin()->first;
}
double area(const Body &body) {
    double total{};
    for (const auto &[id, f] : body.surface.faces)
        total += body.surface.area(id);
    return total;
}
bool edge(const Document &doc, Id body, Vec3 a, Vec3 b) {
    const auto &record = *doc.bodies().at(body);
    const auto world = doc.worldTransform(body);
    for (const auto &[id, e] : record.topology.edges) {
        const auto p = world.point(record.surface.vertices.at(e.a)),
                   q = world.point(record.surface.vertices.at(e.b));
        if ((length(p - a) < 1e-6 && length(q - b) < 1e-6) ||
            (length(p - b) < 1e-6 && length(q - a) < 1e-6))
            return true;
    }
    return false;
}
template <class F> void rejects(F fn, QString code = {}) {
    try {
        fn();
    } catch (const std::exception &e) {
        check(code.isEmpty() || automationFailure(e)["code"] == code, e.what());
        return;
    }
    throw std::runtime_error("Expected intersection rejection");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto a = horizontal(doc), b = vertical(doc), face = firstFace(doc, a);
        auto colored = std::make_shared<Body>(*doc.bodies().at(a));
        colored->faceColors[face] = {.2f, .3f, .4f};
        doc.apply({"Color", {{a, doc.bodies().at(a), colored}}}, doc.revision());
        const auto source = doc.bodies().at(a), reference = doc.bodies().at(b);
        const auto before = encodeDocument(doc);
        const auto history = doc.history().total;
        const auto request = batch(doc, {command(a, face)});
        const auto preview = previewBatch(doc, request);
        check(encodeDocument(doc) == before,
              "Intersection preview leaves document and allocators unchanged");
        const auto result = executeBatch(doc, request);
        check(result["changes"] == preview["changes"],
              "Intersection preview and publication mappings agree");
        check(doc.history().total == history + 1 && doc.bodies().at(a)->surface.faces.size() == 2,
              "Context intersection splits target once");
        check(doc.bodies().at(b) == reference, "Reference context remains exactly unchanged");
        check(edge(doc, a, {1, 0, 0}, {1, 3, 0}),
              "Target has independently expected intersection edge");
        near(area(*doc.bodies().at(a)), 12, "Intersection preserves target coverage");
        for (const auto &[id, f] : doc.bodies().at(a)->surface.faces)
            check(faceColor(*doc.bodies().at(a), id) == faceColor(*source, face),
                  "All descendants inherit face appearance");
        const auto descendants = result["changes"]
                                     .toObject()[QString::number(a)]
                                     .toObject()["faces"]
                                     .toObject()["descendants"]
                                     .toObject()[QString::number(face)]
                                     .toArray();
        check(descendants.size() == 2, "Original face maps to both partitions");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc),
              "Intersection topology and colors persist");
        const auto after = doc.bodies().at(a);
        doc.undo();
        check(doc.bodies().at(a)->surface.vertices == source->surface.vertices &&
                  doc.bodies().at(a)->surface.faces == source->surface.faces,
              "Undo restores exact source geometry");
        doc.redo();
        check(doc.bodies().at(a)->surface == after->surface, "Redo restores exact intersection");
        rejects([&] { executeBatch(doc, request); });
        const auto stable = encodeDocument(doc);
        check(intersectSelected(doc, {{a, SelectionKind::Face, firstFace(doc, a)}},
                                IntersectionMode::Context, 0)
                  .empty(),
              "Repeated existing seam is a core no-op");
        check(encodeDocument(doc) == stable, "No-op preserves allocator and history");
        doc.undo();
        const auto rollback = encodeDocument(doc);
        rejects([&] { executeBatch(doc, batch(doc, {command(a, face), command(a, 999)})); },
                "INTERSECTION_SCOPE");
        check(encodeDocument(doc) == rollback,
              "Late intersection batch failure rolls back earlier split");
        auto both = command(a, face, "selected");
        both["entities"] =
            QJsonArray{QJsonObject{{"body", QString::number(a)}, {"face", QString::number(face)}},
                       QJsonObject{{"body", QString::number(b)},
                                   {"face", QString::number(firstFace(doc, b))}}};
        executeBatch(doc, batch(doc, {both}));
        check(edge(doc, a, {1, 0, 0}, {1, 3, 0}) && edge(doc, b, {1, 0, 0}, {1, 3, 0}),
              "Selected mode inserts matching seams in both independent bodies");
        near(area(*doc.bodies().at(b)), 10, "Interior reference seam does not alter coverage");
        Document shared;
        const auto sb = horizontal(shared);
        auto joined = std::make_shared<Body>(*shared.bodies().at(sb));
        const auto crossing =
            joined->surface.addFace({{{1, 0, -1}, {1, 3, -1}, {1, 3, 1}, {1, 0, 1}}});
        shared.apply({"Crossing faces", {{sb, shared.bodies().at(sb), joined}}}, shared.revision());
        intersectSelected(shared,
                          {{sb, SelectionKind::Face, 5}, {sb, SelectionKind::Face, crossing}},
                          IntersectionMode::Selected, 0);
        check(shared.bodies().at(sb)->surface.faces.size() == 4,
              "Two selected planes in one body both acquire their face partitions");
        bool radial = false;
        for (auto edge : shared.bodies().at(sb)->surface.edges())
            radial |= edge.faces.size() == 4;
        check(radial, "Crossing faces share one authoritative four-incident intersection edge");
        Document holes;
        const auto ring = horizontal(holes, true);
        vertical(holes);
        executeBatch(holes, batch(holes, {command(ring, firstFace(holes, ring))}));
        near(area(*holes.bodies().at(ring)), 10, "Intersection retains explicit hole coverage");
        check(edge(holes, ring, {1, 0, 0}, {1, 1, 0}) && edge(holes, ring, {1, 2, 0}, {1, 3, 0}),
              "Intersection stops at both hole boundaries");
        Document coplanar;
        const auto big = horizontal(coplanar);
        const auto small = coplanar.addFace({{{1, 1, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}}});
        const auto keep = coplanar.bodies().at(small);
        executeBatch(coplanar, batch(coplanar, {command(big, firstFace(coplanar, big))}));
        check(coplanar.bodies().at(big)->surface.faces.size() == 2 &&
                  coplanar.bodies().at(small) == keep,
              "Contained coplanar reference inserts a boundary without merging bodies");
        near(area(*coplanar.bodies().at(big)), 12,
             "Coplanar subdivision preserves source material area");
        Document placed;
        const auto target = horizontal(placed);
        const auto group = createGroup(placed, {target});
        placed.transform(group,
                         Transform::translation({10, 5, 2}) * Transform::rotation({0, 1, 0}, .3));
        placed.transform(target, Transform::scaling({-2, 3, 1}), group);
        const auto world = placed.worldTransform(target);
        const auto cut = vertical(placed, world);
        setEntityState(placed, cut, {}, true);
        const auto outside = placed.bodies().at(cut), parent = placed.bodies().at(group);
        const auto tf = firstFace(placed, target);
        check(intersectSelected(placed, {{target, SelectionKind::Face, tf}},
                                IntersectionMode::Context, group)
                  .empty(),
              "Context mode respects the enclosing group boundary");
        executeBatch(placed, batch(placed, {command(target, tf, "model", group)}));
        check(edge(placed, target, world.point({1, 0, 0}), world.point({1, 3, 0})),
              "Model intersection handles nested mirrored nonuniform world transforms");
        check(placed.bodies().at(cut) == outside && placed.bodies().at(group) == parent,
              "Locked reference and group records remain untouched");
        placed.undo();
        setEntityState(placed, cut, true, {});
        check(intersectSelected(placed, {{target, SelectionKind::Face, tf}},
                                IntersectionMode::Model, group)
                  .empty(),
              "Persistently hidden references do not intersect");
        const auto lockedBytes = encodeDocument(placed);
        rejects([&] { executeBatch(placed, batch(placed, {command(target, tf, "model", 0)})); },
                "INTERSECTION_SCOPE");
        check(encodeDocument(placed) == lockedBytes,
              "Wrong editable context rejects without mutation");
        Document component;
        horizontal(component);
        createComponent(component, 1);
        const auto instance =
            placeComponent(component, 1, Transform::translation({10, 0, 0})).instance;
        const auto member = component.instances().at(instance)->members.at(2);
        const auto external = vertical(component, Transform::translation({10, 0, 0}));
        const auto sibling = component.bodies().at(2), ref = component.bodies().at(external);
        const auto definition = component.definitions().at(1);
        executeBatch(
            component,
            batch(component,
                  {QJsonObject{{"command", "component.edit_instance"},
                               {"body", QString::number(instance)},
                               {"commands", QJsonArray{command(member, 5, "model", instance)}}}}));
        check(edge(component, member, {11, 0, 0}, {11, 3, 0}),
              "Model intersection in instance scope reads the outer scene");
        check(component.bodies().at(2) == sibling && component.bodies().at(external) == ref &&
                  component.definitions().at(1) == definition,
              "Unique instance intersection preserves siblings, source definition and outer "
              "references");
        std::cout << "Intersection selected/context/model scope, transforms, mappings, "
                     "preview/rollback/history, holes, persistence and outer-scene component "
                     "references passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
