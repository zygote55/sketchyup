#include "automation/commands.hpp"
#include "automation/session.hpp"
#include "core/appearance.hpp"
#include "core/components.hpp"
#include "geometry/offset.hpp"
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
std::vector<Vec3> box(double x, double y, double w, double h) {
    return {{x, y, 0}, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}};
}
QJsonObject request(const Document &doc, const QJsonArray &commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(Id body, Id face, double distance, QString space = "local") {
    return {{"command", "geometry.offset"},
            {"body", QString::number(body)},
            {"face", QString::number(face)},
            {"distance", distance},
            {"space", space}};
}
double area(const Surface &s) {
    double a{};
    for (const auto &[id, f] : s.faces)
        a += s.area(id);
    return a;
}
bool sameGeometry(const Body &a, const Body &b) {
    return a.surface.vertices == b.surface.vertices && a.surface.faces == b.surface.faces &&
           a.surface.wires == b.surface.wires && a.topology.edges == b.topology.edges &&
           a.faceColors == b.faceColors && a.faceMaterials == b.faceMaterials;
}
template <class F> void rejects(F fn, const QString &code = {}) {
    try {
        fn();
    } catch (const std::exception &e) {
        check(code.isEmpty() || automationFailure(e)["code"] == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejected offset");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        auto body = doc.addFace({box(0, 0, 4, 3)});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        const auto other = doc.addFace({box(10, 10, 2, 2)});
        const auto untouched = doc.bodies().at(other);
        auto colored = std::make_shared<Body>(*doc.bodies().at(body));
        colored->faceColors[face] = {.2f, .3f, .4f};
        doc.apply({"Set face color", {{body, doc.bodies().at(body), colored}}}, doc.revision());
        const auto old = doc.bodies().at(body);
        const auto before = encodeDocument(doc);
        const auto history = doc.historyBytes();
        const auto batch = request(doc, {command(body, face, -.5)});
        const auto preview = previewBatch(doc, batch);
        check(encodeDocument(doc) == before && doc.historyBytes() == history,
              "Preview is read only");
        const auto count = doc.history().total;
        const auto result = executeBatch(doc, batch);
        check(result["changes"] == preview["changes"], "Preview and publication lineage agree");
        check(doc.history().total == count + 1, "Offset creates one history entry");
        const auto after = doc.bodies().at(body);
        check(after->surface.faces.size() == 2 && after->surface.wires.empty(),
              "Inset forms inner face and surrounding ring");
        near(area(after->surface), 12, "Inset preserves source coverage");
        for (const auto &[id, f] : after->surface.faces) {
            near(after->surface.area(id), 6, "Inset patch and ring each have analytic area");
            check(faceColor(*after, id) == faceColor(*old, face),
                  "All descendants inherit source color");
        }
        check(doc.bodies().at(other) == untouched, "Other contexts preserve their records");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc),
              "Offset topology and colors persist");
        doc.undo();
        check(sameGeometry(*doc.bodies().at(body), *old), "Undo restores exact topology and color");
        doc.redo();
        check(sameGeometry(*doc.bodies().at(body), *after), "Redo restores exact new topology");
        rejects([&] { executeBatch(doc, batch); });
        doc.undo();
        auto stable = encodeDocument(doc);
        rejects([&] { executeBatch(doc, request(doc, {command(body, face, -2)})); },
                "OFFSET_COLLAPSED");
        check(encodeDocument(doc) == stable, "Collapse retains source and history");
        rejects([&] {
            executeBatch(doc, request(doc, {command(body, face, -.5), command(body, 999, -.5)}));
        });
        check(encodeDocument(doc) == stable, "Late batch failure rolls back offset");
        rejects([&] { executeBatch(doc, request(doc, {command(body, face, 0)})); });
        check(doc.offsetFace(body, face, 0).empty() && encodeDocument(doc) == stable,
              "Zero is a core no-op; unchanged command batches reject without history");
        rejects([&] { executeBatch(doc, request(doc, {command(body, face, .2, "screen")})); });
        auto locked = std::make_shared<Body>(*doc.bodies().at(body));
        locked->locked = true;
        doc.apply({"Lock", {{body, doc.bodies().at(body), locked}}}, doc.revision());
        const auto lockedBytes = encodeDocument(doc);
        rejects([&] { executeBatch(doc, request(doc, {command(body, face, -.5)})); });
        check(encodeDocument(doc) == lockedBytes, "Locked geometry rejects without modification");
        doc.undo();
        doc.offsetFace(body, face, .5);
        near(area(doc.bodies().at(body)->surface), 20, "Outset adds exterior coverage");
        check(doc.bodies().at(body)->surface.faces.contains(face),
              "Outset retains original face identity");
        Document ring;
        const auto rb = ring.addFace({box(0, 0, 10, 8), box(3, 2, 4, 4)});
        const auto rf = ring.bodies().at(rb)->surface.faces.begin()->first;
        ring.offsetFace(rb, rf, -.5);
        near(area(ring.bodies().at(rb)->surface), 64,
             "Inset around holes preserves original void and coverage");
        check(ring.bodies().at(rb)->surface.faces.size() == 3,
              "Inset makes outer band, inner region and hole band");
        ring.undo();
        ring.offsetFace(rb, rf, 1);
        near(area(ring.bodies().at(rb)->surface), 104,
             "Outset grows outer coverage without filling original hole");
        check(ring.bodies().at(rb)->surface.wires.size() == 4,
              "Contracted hole outline remains editable wire in original void");
        Document solid;
        const auto sb = solid.addFace({box(0, 0, 4, 3)});
        solid.extrude(sb, solid.bodies().at(sb)->surface.faces.begin()->first, 2);
        Id top{};
        for (const auto &[id, f] : solid.bodies().at(sb)->surface.faces)
            if (solid.bodies().at(sb)->surface.normal(id).z > .99)
                top = id;
        solid.offsetFace(sb, top, -.25);
        double volume{};
        for (auto t : solid.bodies().at(sb)->surface.triangles())
            volume += dot(t.a, cross(t.b, t.c)) / 6;
        near(volume, 24, "Offset on a solid preserves volume");
        for (auto e : solid.bodies().at(sb)->surface.edges())
            check(e.faces.size() == 2, "Solid offset has closed radial incidence");
        Document placed;
        placed.addFace({box(0, 0, 4, 3)});
        createComponent(placed, 1);
        const auto instance =
            placeComponent(placed, 1,
                           Transform::translation({10, 5, 2}) * Transform::scaling({-2, 3, 1}))
                .instance;
        const auto member = placed.instances().at(instance)->members.at(2);
        const auto sibling = placed.bodies().at(2);
        const auto def = placed.definitions().at(1);
        const QJsonObject scoped{{"command", "component.edit_instance"},
                                 {"body", QString::number(instance)},
                                 {"commands", QJsonArray{command(member, 5, -.5, "world")}}};
        executeBatch(placed, request(placed, {scoped}));
        check(placed.bodies().at(2) == sibling && placed.definitions().at(1) == def,
              "Instance offset preserves original definition and sibling");
        bool exact = false;
        for (const auto &[id, f] : placed.bodies().at(member)->surface.faces) {
            if (std::abs(placed.worldArea(member, id) - 56) > 1e-6)
                continue;
            exact = true;
            for (auto vid : f.loops[0]) {
                auto p = placed.worldTransform(member).point(
                    placed.bodies().at(member)->surface.vertices.at(vid));
                check((std::abs(p.x - 2.5) < tolerance || std::abs(p.x - 9.5) < tolerance) &&
                          (std::abs(p.y - 5.5) < tolerance || std::abs(p.y - 13.5) < tolerance),
                      "World distance survives mirrored nonuniform component scale");
            }
        }
        check(exact, "World inset has independently expected area");
        std::cout << "Offset command preview/history, coverage/holes, solid and instance-world "
                     "scope passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
