#include "automation/commands.hpp"
#include "automation/session.hpp"
#include "core/appearance.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/profile_sweep.hpp"
#include "core/selection.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
Id number(QJsonValue value) { return value.toString().toULongLong(); }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(Id body, Id face,
                    QJsonArray path = {QJsonArray{0, 0, 0}, QJsonArray{0, 0, 3},
                                       QJsonArray{3, 0, 3}}) {
    return {{"command", "geometry.sweep"},
            {"body", QString::number(body)},
            {"face", QString::number(face)},
            {"path", path}};
}
Id profile(Document &doc) {
    return doc.addFace({{{-.2, -.1, 0}, {.2, -.1, 0}, {.2, .1, 0}, {-.2, .1, 0}}});
}
void volume(const Document &doc, Id body, double expected) {
    auto surface = doc.bodies().at(body)->surface;
    for (auto &[id, point] : surface.vertices)
        point = doc.worldTransform(body).point(point);
    const auto solid = inspectSolid(surface, Topology::rebuild(surface, {}));
    check(solid.volume && std::abs(*solid.volume - expected) < 1e-6,
          "World geometry has independently expected solid volume");
}
template <class F> void rejects(F fn, QString code = {}) {
    try {
        fn();
    } catch (const std::exception &error) {
        check(code.isEmpty() || automationFailure(error)["code"] == code, error.what());
        return;
    }
    throw std::runtime_error("Expected rejected sweep");
}
void mappings(const Document &doc, const QJsonObject &record) {
    const auto body = number(record["body"]);
    std::set<Id> mapped;
    for (auto value : record["caps"].toArray())
        mapped.insert(number(value));
    const auto sides = record["sides"].toArray();
    check(sides.size() == 4, "All source boundary edges have a mapping");
    for (auto value : sides) {
        const auto side = value.toObject();
        check(side["vertices"].toArray().size() == 2, "Boundary key is source vertex pair");
        for (auto value : side["faces"].toArray())
            mapped.insert(number(value));
    }
    check(mapped.size() == doc.bodies().at(body)->surface.faces.size(),
          "Cap and side mappings cover every generated face");
    for (auto face : mapped)
        check(doc.bodies().at(body)->surface.faces.contains(face), "Mapped face exists");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto source = profile(doc);
        const auto face = doc.bodies().at(source)->surface.faces.begin()->first;
        const auto other = profile(doc);
        doc.move(other, {10, 0, 0});
        const auto untouched = doc.bodies().at(other);
        const auto material = createMaterial(doc, "Profile", {.2f, .3f, .4f});
        assignMaterial(doc, source, face, material, true, true);
        const auto original = doc.bodies().at(source);
        Selection selection;
        selection.sync(doc);
        selection.apply(doc, {{source, SelectionKind::Face, face}}, SelectionMode::Replace);
        const auto selected = selection.entities();
        const auto before = encodeDocument(doc);
        const auto historyBytes = doc.historyBytes();
        const auto request = batch(doc, {command(source, face)});
        const auto preview = previewBatch(doc, request);
        check(encodeDocument(doc) == before && doc.historyBytes() == historyBytes,
              "Preview leaves source, history and allocator unchanged");
        const auto history = doc.history().total;
        const auto result = executeBatch(doc, request);
        check(result["sweeps"] == preview["sweeps"] && result["changes"] == preview["changes"],
              "Preview and publication agree on generated IDs and mappings");
        const auto record = result["sweeps"].toArray().at(0).toObject();
        const auto generated = number(record["body"]);
        mappings(doc, record);
        volume(doc, generated, .48);
        check(doc.history().total == history + 1, "One sweep is one history entry");
        check(doc.bodies().at(source) == original && doc.bodies().at(other) == untouched,
              "Source and unrelated contexts preserve exact records");
        selection.sync(doc);
        check(selection.entities() == selected, "Source profile selection survives sweep");
        for (const auto &[id, f] : doc.bodies().at(generated)->surface.faces)
            check(faceMaterials(*doc.bodies().at(generated), id) == faceMaterials(*original, face),
                  "Generated faces retain profile front/back materials");
        const auto output = doc.bodies().at(generated);
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc),
              "Sweep topology and materials persist");
        doc.undo();
        check(!doc.bodies().contains(generated) &&
                  doc.bodies().at(source)->surface == original->surface,
              "Undo removes only the result and restores source exactly");
        doc.redo();
        check(doc.bodies().at(generated)->surface == output->surface, "Redo restores exact result");
        rejects([&] { executeBatch(doc, request); });
        auto stable = encodeDocument(doc);
        rejects(
            [&] {
                executeBatch(doc, batch(doc, {command(source, face,
                                                      {QJsonArray{0, 0, 0}, QJsonArray{0, 0, .05},
                                                       QJsonArray{.05, 0, .05}})}));
            },
            "SWEEP_SELF_INTERSECTION");
        rejects(
            [&] { executeBatch(doc, batch(doc, {command(source, face), command(source, 999)})); });
        check(encodeDocument(doc) == stable,
              "Invalid geometry and late batch failures roll back atomically");
        auto wrong = command(source, face);
        wrong["closed"] = "yes";
        rejects([&] { executeBatch(doc, batch(doc, {wrong})); });
        setEntityState(doc, source, {}, true);
        stable = encodeDocument(doc);
        rejects([&] { executeBatch(doc, batch(doc, {command(source, face)})); }, "SWEEP_LOCKED");
        check(encodeDocument(doc) == stable, "Locked source cannot be used to evade edit guard");
        doc.undo();
        // A later erase removes the corresponding generated mapping from the receipt.
        auto prune = command(source, face);
        const auto next = doc.nextId();
        const auto expected =
            sweepProfile(original->surface, face, {{0, 0, 0}, {0, 0, 3}, {3, 0, 3}});
        const auto deleted = expected.caps.front();
        const auto pruned =
            executeBatch(doc, batch(doc, {prune, QJsonObject{{"command", "geometry.erase_face"},
                                                             {"body", QString::number(next)},
                                                             {"face", QString::number(deleted)}}}));
        check(pruned["sweeps"].toArray().at(0).toObject()["caps"].toArray().size() == 1,
              "Receipt mappings omit faces erased later in the batch");
        doc.undo();
        Document placed;
        const auto ps = profile(placed);
        const auto pf = placed.bodies().at(ps)->surface.faces.begin()->first;
        const auto parent = createGroup(placed, {ps});
        placed.transform(parent,
                         Transform::translation({10, 5, 2}) * Transform::rotation({0, 1, 0}, .3));
        placed.transform(ps, Transform::scaling({-2, 3, 1}), parent);
        const auto world = placed.worldTransform(ps);
        QJsonArray path;
        for (auto point : std::vector<Vec3>{{0, 0, 0}, {0, 0, 3}, {3, 0, 3}}) {
            const auto p = world.point(point);
            path.append(QJsonArray{p.x, p.y, p.z});
        }
        auto wc = command(ps, pf, path);
        wc["space"] = "world";
        const auto wr = executeBatch(placed, batch(placed, {wc}));
        const auto wb = number(wr["created"].toArray()[0]);
        volume(placed, wb, 4.32);
        check(placed.bodies().at(wb)->parent == parent &&
                  placed.bodies().at(ps)->transform == Transform::scaling({-2, 3, 1}),
              "World sweep retains nested context and mirrored source transform");
        Document component;
        profile(component);
        createComponent(component, 1);
        const auto instance =
            placeComponent(component, 1, Transform::translation({10, 0, 0})).instance;
        const auto member = component.instances().at(instance)->members.at(2);
        const auto sibling = component.bodies().at(2);
        const auto definition = component.definitions().at(1);
        const auto cr = executeBatch(
            component,
            batch(component, {QJsonObject{{"command", "component.edit_instance"},
                                          {"body", QString::number(instance)},
                                          {"commands", QJsonArray{command(member, 5)}}}}));
        check(component.bodies().at(2) == sibling && component.definitions().at(1) == definition,
              "Unique-instance sweep preserves original definition and sibling");
        const auto cm = cr["sweeps"].toArray().at(0).toObject();
        check(number(cm["sourceBody"]) == member,
              "Scoped source mapping resolves to scene identity");
        mappings(component, cm);
        volume(component, number(cm["body"]), .48);
        std::cout << "Sweep command mappings, preview/rollback/history, selection, persistence, "
                     "materials and transformed component scope passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
