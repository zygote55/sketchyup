#include "automation/commands.hpp"
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/hosted_components.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QJsonObject request(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject run(Document &doc, QJsonArray commands) {
    return executeBatch(doc, request(doc, commands));
}
QJsonObject adopt(Id room) {
    return {{"command", "assembly.room.adopt_hosted"}, {"body", QString::number(room)}};
}
void erasePlacement(Document &doc, Id root) {
    Selection selection;
    selection.sync(doc);
    selection.enter(doc, doc.bodies().at(root)->parent);
    selection.apply(doc, {{root, SelectionKind::Body, 0}}, SelectionMode::Replace);
    eraseSelected(doc, selection);
}
void volume(const Document &doc, Id wall, double expected) {
    const auto measured = measureEntity(doc, {wall, SelectionKind::Body, 0}).local.volume;
    check(measured && std::abs(*measured - expected) < 1e-6,
          "Adopted wall has independently expected material volume");
}
template <class F> void rejects(Document &doc, F execute) {
    const auto before = encodeContainer(doc);
    const auto history = doc.history().total;
    bool rejected = false;
    try {
        execute();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && encodeContainer(doc) == before && doc.history().total == history,
          "Invalid adoption preserves all document bytes and history");
}
struct Room {
    Document doc;
    Id room{}, wall{}, first{}, second{}, definition{};
    Room() {
        const auto result = run(doc, {QJsonObject{{"command", "assembly.room"}}});
        const auto report = result["recipeOperations"].toArray().first().toObject();
        room = report["room"].toString().toULongLong();
        wall = report["wall"].toString().toULongLong();
        first = report["windows"].toArray()[0].toObject()["body"].toString().toULongLong();
        second = report["windows"].toArray()[1].toObject()["body"].toString().toULongLong();
        definition = doc.instances().at(first)->definition;
    }
};
void adoptionAndLifecycle() {
    Room f;
    auto &doc = f.doc;
    auto painted = std::make_shared<Body>(*doc.bodies().at(f.wall));
    Id reveal{};
    for (const auto &[face, value] : painted->surface.faces)
        if (value.loops.size() == 1 &&
            std::all_of(value.loops.front().begin(), value.loops.front().end(), [&](Id v) {
                return std::abs(painted->surface.vertices.at(v).x - .9) < tolerance;
            }))
            reveal = face;
    check(reveal, "Recipe fixture locates a real opening reveal");
    painted->faceColors[reveal] = {.9f, .2f, .1f};
    painted->faceMaterials[reveal] = {doc.materials().begin()->first,
                                      doc.materials().rbegin()->first};
    painted->edgeAppearances[painted->topology.edges.begin()->first] = {true, false, false};
    doc.apply({"Paint recipe reveal", {{f.wall, doc.bodies().at(f.wall), painted}}},
              doc.revision());
    const auto original = encodeContainer(doc);
    const auto originalBodies = doc.bodies();
    const auto wall = doc.bodies().at(f.wall);
    const auto depth = doc.history().total;
    const auto preview = previewBatch(doc, request(doc, {adopt(f.room)}));
    check(encodeContainer(doc) == original, "Recipe adoption preview is private");
    const auto committed = run(doc, {adopt(f.room)});
    check(preview["changes"] == committed["changes"] && doc.history().total == depth + 1,
          "Adoption agrees with preview and commits one Undo item");
    check(doc.hostedComponents().hosts.size() == 1 &&
              doc.hostedComponents().attachments.size() == 2 &&
              doc.hostedComponents().attachments.at(f.first)->host == f.wall &&
              doc.hostedComponents().attachments.at(f.second)->host == f.wall &&
              doc.definitions().at(f.definition)->glue->cutsOpening,
          "Explicit authored room adoption binds both shared window placements");
    const auto &vertices = doc.bodies().at(f.wall)->surface.vertices;
    check(vertices.size() == wall->surface.vertices.size(), "Adoption preserves vertex count");
    for (const auto &[id, value] : wall->surface.vertices)
        check(vertices.contains(id) && length(vertices.at(id) - value) < 1e-12,
              "Adoption preserves vertex IDs and coordinates to floating-point precision");
    check(doc.bodies().at(f.wall)->surface.faces.size() == wall->surface.faces.size(),
          "Adoption preserves face count");
    for (const auto &[id, value] : wall->surface.faces)
        check(doc.bodies().at(f.wall)->surface.faces.contains(id),
              "Adoption preserves every wall and reveal face identity");
    for (const auto &[id, value] : originalBodies)
        if (id != f.wall)
            check(*doc.bodies().at(id) == *value, "Adoption preserves every other scene body");
    check(doc.bodies().at(f.wall)->faceColors == wall->faceColors &&
              doc.bodies().at(f.wall)->materials == wall->materials &&
              doc.bodies().at(f.wall)->faceMaterials == wall->faceMaterials &&
              doc.bodies().at(f.wall)->edgeAppearances == wall->edgeAppearances,
          "Adoption preserves wall coordinates, stable vertex IDs and current appearance");
    volume(doc, f.wall, 9.888);
    const auto saved = encodeContainer(doc);
    check(encodeContainer(decodeContainer(saved)) == saved, "Adopted recipe persists exactly");
    doc.undo();
    check(doc.hostedComponents().attachments.empty() && !doc.definitions().at(f.definition)->glue &&
              doc.bodies().at(f.wall)->surface.vertices == wall->surface.vertices &&
              doc.bodies().at(f.wall)->surface.faces == wall->surface.faces,
          "One Undo restores the original unbound recipe and wall topology");
    doc.redo();
    rejects(doc, [&] { run(doc, {adopt(f.room)}); });
    const auto originalOpening = doc.hostedComponents().hosts.at(f.wall)->openings.at(f.first);
    doc.move(f.first, {.2, 0, 0});
    volume(doc, f.wall, 9.888);
    check(doc.hostedComponents().hosts.at(f.wall)->openings.at(f.first).jambs ==
              originalOpening.jambs,
          "Ordinary movement updates an adopted opening with stable reveals");
    erasePlacement(doc, f.first);
    volume(doc, f.wall, 10.128);
    check(doc.hostedComponents().attachments.size() == 1 &&
              doc.hostedComponents().attachments.contains(f.second),
          "Deleting an adopted window restores only its opening");
}
void affineAssembly() {
    Room f;
    f.doc.transform(f.room, Transform::translation({8, 2, 1}) * Transform::rotation({0, 0, 1}, .4) *
                                Transform::scaling({-1.2, .8, 1.5}));
    const auto pose = f.doc.worldTransform(f.first);
    run(f.doc, {adopt(f.room)});
    check(f.doc.worldTransform(f.first) == pose,
          "Adopting a reflected nonuniform room retains the exact window pose");
    volume(f.doc, f.wall, 9.888);
    f.doc.transform(f.first, Transform::translation({1.7, 0, .9}), f.room);
    volume(f.doc, f.wall, 9.888);
    const auto bytes = encodeContainer(f.doc);
    check(encodeContainer(decodeContainer(bytes)) == bytes,
          "Affinely hosted recipe attachment frames persist exactly");
    detachComponent(f.doc, f.first);
    volume(f.doc, f.wall, 10.128);
}
void resizeAndScope() {
    Room f;
    run(f.doc, {adopt(f.room)});
    const auto other = f.doc.instances().at(f.second);
    const auto originalDefinition = f.doc.definitions().at(f.definition);
    run(f.doc, {QJsonObject{{"command", "assembly.window.resize"},
                            {"body", QString::number(f.first)},
                            {"width", 1.4},
                            {"scope", "instance"}}});
    volume(f.doc, f.wall, 9.848);
    check(f.doc.instances().at(f.second) == other &&
              f.doc.definitions().at(f.definition) == originalDefinition &&
              f.doc.instances().at(f.first)->definition != f.definition,
          "Authored resize uses automatic host regeneration and preserves the other definition");
    check(f.doc.hostedComponents().attachments.size() == 2,
          "Instance-only resize retains both explicit relationships");
}
void invalidAdoption() {
    Room f;
    auto invalid = adopt(f.room);
    invalid["guess"] = true;
    rejects(f.doc, [&] { run(f.doc, {invalid}); });
    rejects(f.doc, [&] { run(f.doc, {adopt(f.wall)}); });
    f.doc.move(f.first, {.2, 0, 0});
    rejects(f.doc, [&] { run(f.doc, {adopt(f.room)}); });
    f.doc.undo();
    const auto outside = placeComponent(f.doc, f.definition).instance;
    rejects(f.doc, [&] { run(f.doc, {adopt(f.room)}); });
    erasePlacement(f.doc, outside);
    auto changed = std::make_shared<Body>(*f.doc.bodies().at(f.wall));
    changed->locked = true;
    f.doc.apply({"Lock wall", {{f.wall, f.doc.bodies().at(f.wall), changed}}}, f.doc.revision());
    rejects(f.doc, [&] { run(f.doc, {adopt(f.room)}); });
    f.doc.undo();
    auto ambiguous = std::make_shared<Body>(*f.doc.bodies().at(f.wall));
    ambiguous->properties["recipe.window.1"] = std::to_string(f.first);
    f.doc.apply({"Ambiguous recipe slot", {{f.wall, f.doc.bodies().at(f.wall), ambiguous}}},
                f.doc.revision());
    rejects(f.doc, [&] {
        run(f.doc, {QJsonObject{{"command", "material.color"},
                                {"body", QString::number(f.wall)},
                                {"color", QJsonArray{.2, .4, .6}}},
                    adopt(f.room)});
    });
    f.doc.undo();
    createComponent(f.doc, f.room, "Shared room");
    rejects(f.doc, [&] { run(f.doc, {adopt(f.room)}); });
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        adoptionAndLifecycle();
        resizeAndScope();
        affineAssembly();
        invalidAdoption();
        std::cout << "Explicit recipe adoption, exact geometry, opening lifecycle, scoped resize, "
                     "persistence and rejection guards passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
