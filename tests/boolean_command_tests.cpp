#include "automation/commands.hpp"
#include "automation/session.hpp"
#include "core/appearance.hpp"
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/solid_boolean.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message) {
    check(std::abs(actual - expected) < 1e-6, message);
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(Id body, Id tool, QString operation = "subtract", bool keep = true,
                    Id context = 0) {
    return {{"command", "geometry.boolean"}, {"body", QString::number(body)},
            {"tool", QString::number(tool)}, {"context", QString::number(context)},
            {"operation", operation},        {"keepOperands", keep}};
}
Id box(Document &doc, Vec3 o = {}, Vec3 size = {2, 2, 2}) {
    const auto id = doc.addFace(
        {{o, o + Vec3{size.x, 0, 0}, o + Vec3{size.x, size.y, 0}, o + Vec3{0, size.y, 0}}});
    doc.extrude(id, 5, size.z);
    return id;
}
QJsonObject operation(const QJsonObject &result) {
    return result["booleans"].toArray().at(0).toObject();
}
QJsonObject part(const QJsonObject &result) {
    return operation(result)["parts"].toArray().at(0).toObject();
}
Id output(const QJsonObject &result) { return part(result)["body"].toString().toULongLong(); }
double volume(const Document &doc, Id body) {
    auto surface = doc.bodies().at(body)->surface;
    const auto frame = doc.worldTransform(body);
    for (auto &[id, p] : surface.vertices)
        p = frame.point(p);
    const auto report = inspectSolid(surface, Topology::rebuild(surface, {}));
    check(report.status == "solid" && report.volume.has_value(),
          "Command result independently classifies as solid");
    return *report.volume;
}
template <class F> void rejects(F fn, QString code = {}) {
    try {
        fn();
    } catch (const std::exception &e) {
        check(code.isEmpty() || automationFailure(e)["code"] == code, e.what());
        return;
    }
    throw std::runtime_error("Expected Boolean command rejection");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto a = box(doc), b = box(doc, {1, 0, 0});
        const auto front = createMaterial(doc, "Front", {.8f, .2f, .1f});
        const auto back = createMaterial(doc, "Back", {.1f, .2f, .8f});
        assignMaterial(doc, b, {}, front, true, false);
        assignMaterial(doc, b, {}, back, false, true);
        auto styled = std::make_shared<Body>(*doc.bodies().at(a));
        styled->faceColors[5] = {.2f, .7f, .4f};
        styled->tag = createTag(doc, "Solids");
        doc.apply({"Style", {{a, doc.bodies().at(a), styled}}}, doc.revision());
        const auto source = doc.bodies().at(a), tool = doc.bodies().at(b);
        Selection selection;
        selection.apply(doc, {{a, SelectionKind::Body, 0}, {b, SelectionKind::Body, 0}},
                        SelectionMode::Replace);
        const auto selected = selection.entities();
        const auto before = encodeDocument(doc);
        const auto history = doc.history().total;
        const auto request = batch(doc, {command(a, b)});
        const auto preview = previewBatch(doc, request);
        check(encodeDocument(doc) == before, "Preview preserves document, allocator and history");
        const auto result = executeBatch(doc, request);
        check(result["changes"] == preview["changes"] && result["booleans"] == preview["booleans"],
              "Preview and commit share geometry/provenance mappings");
        const auto generated = output(result);
        near(volume(doc, generated), 4, "Subtraction result has analytical world volume");
        near(part(result)["generatedVolume"].toDouble(), 4,
             "Receipt records generated world volume");
        check(doc.history().total == history + 1 && doc.bodies().at(a) == source &&
                  doc.bodies().at(b) == tool,
              "One edit retains exact operand records");
        check(doc.bodies().at(generated)->tag == source->tag, "Result inherits target tag");
        selection.sync(doc);
        check(selection.entities() == selected, "Retained operands preserve source selection");
        bool cuttingFace{};
        for (auto value : part(result)["faces"].toArray()) {
            const auto mapping = value.toObject();
            const auto face = mapping["face"].toString().toULongLong();
            const auto oldFace = mapping["sourceFace"].toString().toULongLong();
            const auto oldBody =
                mapping["sourceBody"].toString().toULongLong() == a ? source : tool;
            auto materials = faceMaterials(*oldBody, oldFace);
            if (mapping["reversed"].toBool())
                std::swap(materials.front, materials.back);
            check(faceMaterials(*doc.bodies().at(generated), face) == materials &&
                      faceColor(*doc.bodies().at(generated), face) == faceColor(*oldBody, oldFace),
                  "Output face inherits its source color and correctly oriented material sides");
            cuttingFace |= oldBody == tool && mapping["reversed"].toBool();
        }
        check(cuttingFace, "Receipt identifies reversed cutting faces");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc),
              "Boolean topology/materials persist exactly");
        const auto outputRecord = doc.bodies().at(generated);
        doc.undo();
        check(!doc.bodies().contains(generated) && doc.bodies().at(a) == source &&
                  doc.bodies().at(b) == tool,
              "Undo removes result and restores operands");
        doc.redo();
        check(doc.bodies().at(generated)->surface == outputRecord->surface,
              "Redo restores exact topology");
        rejects([&] { executeBatch(doc, request); });
        auto stable = encodeDocument(doc);
        rejects([&] { executeBatch(doc, batch(doc, {command(a, b), command(a, 999)})); });
        check(encodeDocument(doc) == stable,
              "Late command failure rolls back generated bodies and allocators");
        auto missing = command(a, b);
        missing.remove("keepOperands");
        rejects([&] { executeBatch(doc, batch(doc, {missing})); });
        setEntityState(doc, b, {}, true);
        stable = encodeDocument(doc);
        rejects([&] { executeBatch(doc, batch(doc, {command(a, b)})); }, "BOOLEAN_SCOPE");
        check(encodeDocument(doc) == stable, "Locked operands cannot bypass scope validation");
        doc.undo();
        const auto consumed = executeBatch(doc, batch(doc, {command(a, b, "union", false)}));
        check(!doc.bodies().contains(a) && !doc.bodies().contains(b),
              "Explicit consume removes both operands");
        near(volume(doc, output(consumed)), 12, "Consumed union has analytical volume");
        selection.sync(doc);
        check(selection.entities().empty(), "Consumed source selection is pruned");
        doc.undo();
        check(doc.bodies().at(a) == source && doc.bodies().at(b) == tool,
              "Undo restores consumed materials and operands");
        const auto prediction = previewBatch(doc, batch(doc, {command(a, b, "union")}));
        const auto next = doc.nextId();
        const QJsonValue erasedFace = part(prediction)["faces"].toArray().at(0).toObject()["face"];
        const auto pruned = executeBatch(
            doc, batch(doc, {command(a, b, "union"), QJsonObject{{"command", "geometry.erase_face"},
                                                                 {"body", QString::number(next)},
                                                                 {"face", erasedFace}}}));
        for (auto value : part(pruned)["faces"].toArray())
            check(value.toObject()["face"] != erasedFace,
                  "Later face erasure prunes generated mapping");
        Document empty;
        const auto ea = box(empty), eb = box(empty, {3, 0, 0});
        stable = encodeDocument(empty);
        rejects(
            [&] { executeBatch(empty, batch(empty, {command(ea, eb, "intersection", true)})); });
        check(encodeDocument(empty) == stable,
              "Empty retained result creates no history or allocator changes");
        const auto emptyResult =
            executeBatch(empty, batch(empty, {command(ea, eb, "intersection", false)}));
        check(empty.bodies().empty() && operation(emptyResult)["parts"].toArray().empty(),
              "Explicit consume permits an empty intersection result");
        empty.undo();
        check(empty.bodies().size() == 2, "Undo restores both operands of empty intersection");
        Document placed;
        const auto pa = box(placed), pb = box(placed, {1, 0, 0});
        const auto group = createGroup(placed, {pa, pb});
        const auto transform = Transform::translation({20, -10, 4}) *
                               Transform::rotation({2, 1, 3}, .37) *
                               Transform::scaling({-1.5, .75, 1.2});
        placed.transform(group, transform);
        const auto parent = placed.bodies().at(group);
        const auto nested =
            executeBatch(placed, batch(placed, {command(pa, pb, "subtract", true, group)}));
        near(volume(placed, output(nested)), 5.4,
             "Nested mirrored/nonuniform command uses world geometry");
        check(placed.bodies().at(output(nested))->parent == group &&
                  placed.bodies().at(group) == parent,
              "Boolean result stays in target context and preserves group record");
        stable = encodeDocument(placed);
        rejects([&] { executeBatch(placed, batch(placed, {command(pa, pb)})); }, "BOOLEAN_SCOPE");
        check(encodeDocument(placed) == stable, "Wrong context rejects atomically");
        Document component;
        const auto ca = box(component), cb = box(component, {1, 0, 0});
        const auto root = createGroup(component, {ca, cb});
        const auto definition = createComponent(component, root).definition;
        const auto instance =
            placeComponent(component, definition, Transform::translation({10, 0, 0})).instance;
        const auto ma = component.instances().at(instance)->members.at(ca),
                   mb = component.instances().at(instance)->members.at(cb);
        const auto originalDefinition = component.definitions().at(definition);
        const auto originalA = component.bodies().at(ca), originalB = component.bodies().at(cb);
        const auto scoped =
            QJsonObject{{"command", "component.edit_instance"},
                        {"body", QString::number(instance)},
                        {"commands", QJsonArray{command(ma, mb, "subtract", false, instance)}}};
        const auto scopedBefore = encodeDocument(component);
        const auto scopedPreview = previewBatch(component, batch(component, {scoped}));
        check(encodeDocument(component) == scopedBefore,
              "Unique-instance Boolean preview remains isolated");
        const auto scopedResult = executeBatch(component, batch(component, {scoped}));
        check(scopedResult["booleans"] == scopedPreview["booleans"],
              "Scoped preview and publication map identical scene IDs");
        check(component.definitions().at(definition) == originalDefinition &&
                  component.bodies().at(ca) == originalA &&
                  component.bodies().at(cb) == originalB && !component.bodies().contains(ma) &&
                  !component.bodies().contains(mb),
              "Unique instance consumes only selected instance operands and retains original "
              "definition/sibling");
        const auto scopedBody = output(scopedResult);
        near(volume(component, scopedBody), 4, "Scoped Boolean result has expected volume");
        check(component.bodies().at(scopedBody)->parent == instance &&
                  operation(scopedResult)["sourceBody"] == QString::number(ma) &&
                  operation(scopedResult)["toolBody"] == QString::number(mb),
              "Generated receipt resolves body and consumed operand identities to scene IDs");
        for (auto value : part(scopedResult)["faces"].toArray())
            check(value.toObject()["sourceBody"] == QString::number(ma) ||
                      value.toObject()["sourceBody"] == QString::number(mb),
                  "Scoped face provenance resolves original scene operand IDs");
        reopened = decodeContainer(encodeContainer(component));
        check(encodeDocument(reopened) == encodeDocument(component),
              "Scoped Boolean definition and materials persist");
        component.undo();
        check(component.bodies().contains(ma) && component.bodies().contains(mb),
              "Scoped Undo restores operands");
        component.redo();
        near(volume(component, scopedBody), 4, "Scoped Redo restores result");
        // Inverse-transpose normals are an independent physical-side oracle:
        // rendering preserves the physical front under reflected placements.
        auto physicalNormal = [](const Document &scene, Id body, Id face) {
            const auto n = scene.bodies().at(body)->surface.normal(face);
            const auto inverse = scene.worldTransform(body).inverse();
            return normalized(Vec3{inverse.m[0] * n.x + inverse.m[1] * n.y + inverse.m[2] * n.z,
                                   inverse.m[4] * n.x + inverse.m[5] * n.y + inverse.m[6] * n.z,
                                   inverse.m[8] * n.x + inverse.m[9] * n.y + inverse.m[10] * n.z});
        };
        for (unsigned mirrors = 1; mirrors <= 3; ++mirrors) {
            Document mirrored;
            const auto outer = box(mirrored), inner = box(mirrored, {.5, .5, .5}, {1, 1, 1});
            const auto reflection =
                Transform::translation({2, 0, 0}) * Transform::scaling({-1, 1, 1});
            if (mirrors & 1)
                mirrored.transform(outer, reflection);
            if (mirrors & 2)
                mirrored.transform(inner, reflection);
            const auto front = createMaterial(mirrored, "Physical front", {.8f, .1f, .1f});
            const auto back = createMaterial(mirrored, "Physical back", {.1f, .1f, .8f});
            for (auto body : {outer, inner}) {
                assignMaterial(mirrored, body, {}, front, true, false);
                assignMaterial(mirrored, body, {}, back, false, true);
            }
            const auto receipt = executeBatch(mirrored, batch(mirrored, {command(outer, inner)}));
            const auto body = output(receipt);
            near(volume(mirrored, body), 7, "Independently mirrored operand cavity volume");
            for (auto value : part(receipt)["faces"].toArray()) {
                const auto mapping = value.toObject();
                const auto face = mapping["face"].toString().toULongLong();
                const auto sourceBody = mapping["sourceBody"].toString().toULongLong();
                const auto sourceFace = mapping["sourceFace"].toString().toULongLong();
                const bool reversed = dot(physicalNormal(mirrored, body, face),
                                          physicalNormal(mirrored, sourceBody, sourceFace)) < 0;
                const auto appearance = faceMaterials(*mirrored.bodies().at(body), face);
                check(mapping["reversed"].toBool() == reversed &&
                          appearance.front == (reversed ? back : front) &&
                          appearance.back == (reversed ? front : back),
                      "Reflected operand provenance/materials preserve physical front and back");
            }
        }
        Document cavityDoc;
        const auto outerBody = box(cavityDoc), innerBody = box(cavityDoc, {.5, .5, .5}, {1, 1, 1});
        const auto innerFront = createMaterial(cavityDoc, "Cavity front", {.9f, .1f, .2f});
        const auto innerBack = createMaterial(cavityDoc, "Cavity back", {.1f, .8f, .2f});
        assignMaterial(cavityDoc, innerBody, {}, innerFront, true, false);
        assignMaterial(cavityDoc, innerBody, {}, innerBack, false, true);
        const auto innerRecord = cavityDoc.bodies().at(innerBody);
        const auto cavityRequest =
            batch(cavityDoc, {command(outerBody, innerBody, "subtract", false)});
        const auto cavityPreview = previewBatch(cavityDoc, cavityRequest);
        const auto cavityResult = executeBatch(cavityDoc, cavityRequest);
        const auto cavityBody = output(cavityResult);
        check(cavityPreview["booleans"] == cavityResult["booleans"] &&
                  cavityDoc.bodies().size() == 1,
              "Cavity preview and consumed publication have identical mappings");
        volume(cavityDoc, cavityBody);
        const auto measured = measureEntity(cavityDoc, {cavityBody, SelectionKind::Body, 0});
        check(measured.solid.status == "solid" && measured.world.volume &&
                  std::abs(*measured.world.volume - 7) < 1e-6,
              "Entity measurement reports cavity material volume, not filled outer volume");
        size_t cavityFaces{};
        for (auto value : part(cavityResult)["faces"].toArray()) {
            const auto face = value.toObject();
            if (face["sourceBody"] != QString::number(innerBody))
                continue;
            ++cavityFaces;
            const auto materials = faceMaterials(*cavityDoc.bodies().at(cavityBody),
                                                 face["face"].toString().toULongLong());
            check(face["reversed"].toBool() && materials.front == innerBack &&
                      materials.back == innerFront,
                  "Enclosed cutter front/back materials reverse with cavity face winding");
        }
        check(cavityFaces == 6, "Every enclosed cutter face survives as a cavity boundary");
        reopened = decodeContainer(encodeContainer(cavityDoc));
        check(encodeDocument(reopened) == encodeDocument(cavityDoc),
              "Cavity topology/materials persist exactly");
        cavityDoc.undo();
        check(cavityDoc.bodies().at(innerBody) == innerRecord,
              "Cavity Undo restores consumed tool and materials");
        cavityDoc.redo();
        near(volume(cavityDoc, cavityBody), 7, "Cavity Redo retains material volume");
        Document invalid;
        const auto solid = box(invalid),
                   open = invalid.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        stable = encodeDocument(invalid);
        try {
            executeBatch(invalid, batch(invalid, {command(solid, open, "union", false)}));
            check(false, "Open operand rejects");
        } catch (const BooleanError &e) {
            check(automationFailure(e)["code"] == "BOOLEAN_INVALID_SOLID" &&
                      QString(e.what()).contains("Operand body 2") &&
                      QString(e.what()).contains("edges:"),
                  "Automation reports classified operand and boundary defects");
        }
        check(encodeDocument(invalid) == stable, "Rejected solid never consumes operands");
        std::cout << "Boolean command scope, materials/provenance, preview/rollback, consumption, "
                     "Undo/persistence and unique components passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
