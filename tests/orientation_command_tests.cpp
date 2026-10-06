#include "automation/commands.hpp"
#include "automation/session.hpp"
#include "core/components.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/tags.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject reverse(Id body, Id face, Id context = 0) {
    return {{"command", "geometry.reverse_faces"},
            {"context", QString::number(context)},
            {"entities", QJsonArray{QJsonObject{{"body", QString::number(body)},
                                                {"face", QString::number(face)}}}}};
}
QJsonObject orient(Id body, Id face, Id context = 0) {
    return {{"command", "geometry.orient_faces"},
            {"context", QString::number(context)},
            {"body", QString::number(body)},
            {"face", QString::number(face)}};
}
Id box(Document &doc, Vec3 o = {}) {
    const auto id = doc.addFace({{o, o + Vec3{2, 0, 0}, o + Vec3{2, 2, 0}, o + Vec3{0, 2, 0}}});
    doc.extrude(id, 5, 2);
    return id;
}
template <class F> void rejects(F fn, QString code = {}) {
    try {
        fn();
    } catch (const std::exception &e) {
        check(code.isEmpty() || automationFailure(e)["code"] == code, e.what());
        return;
    }
    throw std::runtime_error("Expected orientation rejection");
}
Vec3 normal(const Document &doc, const Body &body, Id face) {
    const auto n = body.surface.normal(face);
    const auto inverse = doc.worldTransform(body.id).inverse();
    return normalized(Vec3{inverse.m[0] * n.x + inverse.m[1] * n.y + inverse.m[2] * n.z,
                           inverse.m[4] * n.x + inverse.m[5] * n.y + inverse.m[6] * n.z,
                           inverse.m[8] * n.x + inverse.m[9] * n.y + inverse.m[10] * n.z});
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        for (bool reflected : {false, true}) {
            Document doc;
            const auto body = box(doc);
            const auto front = createMaterial(doc, "Front", {.8f, .2f, .1f}, .4f);
            const auto back = createMaterial(doc, "Back", {.1f, .2f, .8f}, .9f);
            assignMaterial(doc, body, {}, front, true, false);
            assignMaterial(doc, body, {}, back, false, true);
            const auto group = createGroup(doc, {body});
            doc.transform(group, Transform::translation({12, -7, 4}) *
                                     Transform::rotation({1, 2, 3}, .4) *
                                     Transform::scaling({reflected ? -1.5 : 1.5, .75, 1.2}));
            auto styled = std::make_shared<Body>(*doc.bodies().at(body));
            styled->tag = createTag(doc, "Shell");
            styled->properties["fixture"] = std::string("Retain metadata");
            styled->faceColors[5] = {.2f, .7f, .1f};
            doc.apply({"Style", {{body, doc.bodies().at(body), styled}}}, doc.revision());
            const auto original = doc.bodies().at(body), parent = doc.bodies().at(group);
            const auto before = encodeDocument(doc);
            const auto history = doc.history().total;
            Selection selection;
            selection.enter(doc, group);
            selection.apply(doc, {{body, SelectionKind::Face, 5}}, SelectionMode::Replace);
            const auto request = batch(doc, {reverse(body, 5, group)});
            const auto preview = previewBatch(doc, request);
            check(encodeDocument(doc) == before,
                  "Reverse preview preserves bytes/history/allocator");
            const auto result = executeBatch(doc, request);
            check(result["changes"] == preview["changes"] && doc.history().total == history + 1,
                  "Reverse preview predicts exact committed change maps in one Undo item");
            const auto changed = doc.bodies().at(body);
            check(changed->surface.vertices == original->surface.vertices &&
                      changed->topology == original->topology &&
                      changed->surface.nextId == original->surface.nextId &&
                      doc.bodies().at(group) == parent &&
                      changed->transform == original->transform && changed->tag == original->tag &&
                      changed->properties == original->properties &&
                      changed->faceColors == original->faceColors &&
                      changed->materials == original->materials,
                  "Reverse retains identities, placement, default appearance and metadata");
            for (const auto &[face, record] : original->surface.faces) {
                const bool flipped =
                    dot(normal(doc, *original, face), normal(doc, *changed, face)) < 0;
                check(flipped == (face == 5), "Only selected face changes physical orientation");
                check(surfaceAppearance(doc.materials(), *changed, face, flipped) ==
                              surfaceAppearance(doc.materials(), *original, face, false) &&
                          surfaceAppearance(doc.materials(), *changed, face, !flipped) ==
                              surfaceAppearance(doc.materials(), *original, face, true),
                      "Physical-side color, opacity and material identity stay unchanged under "
                      "reflection");
            }
            selection.sync(doc);
            check(selection.entities() == SelectionSet{{body, SelectionKind::Face, 5}},
                  "Face selection survives winding changes");
            auto reopened = decodeContainer(encodeContainer(doc));
            check(encodeDocument(reopened) == encodeDocument(doc),
                  "Reversed assignments persist exactly");
            doc.undo();
            check(doc.bodies().at(body) == original, "Reverse Undo restores original body record");
            doc.redo();
            check(*doc.bodies().at(body) == *changed,
                  "Reverse Redo restores all face side assignments");
            rejects([&] { executeBatch(doc, request); });
            const auto reference = original->surface.faces.rbegin()->first;
            const auto repair = batch(doc, {orient(body, reference, group)});
            const auto damaged = encodeDocument(doc);
            const auto repairPreview = previewBatch(doc, repair);
            check(encodeDocument(doc) == damaged, "Orient preview is immutable");
            const auto repaired = executeBatch(doc, repair);
            check(repaired["changes"] == repairPreview["changes"] &&
                      *doc.bodies().at(body) == *original,
                  "Orient restores exact surface and physical materials from unchanged seed");
            check(inspectSolid(doc.bodies().at(body)->surface, doc.bodies().at(body)->topology)
                          .status == "solid",
                  "Reoriented shell validates as solid");
            const auto stable = encodeDocument(doc);
            rejects([&] { executeBatch(doc, batch(doc, {orient(body, reference, group)})); });
            check(encodeDocument(doc) == stable, "Consistent orientation creates no history");
            rejects([&] {
                executeBatch(doc, batch(doc, {reverse(body, 5, group), orient(body, 999, group)}));
            });
            check(encodeDocument(doc) == stable,
                  "Late failure rolls back earlier reversal and appearance");
            rejects([&] { executeBatch(doc, batch(doc, {reverse(body, 5)})); },
                    "ORIENTATION_SCOPE");
            setEntityState(doc, body, {}, true);
            const auto locked = encodeDocument(doc);
            rejects([&] { executeBatch(doc, batch(doc, {reverse(body, 5, group)})); },
                    "ORIENTATION_SCOPE");
            check(encodeDocument(doc) == locked, "Locked faces cannot bypass orientation policy");
        }
        // Multi-body reversal is one atomic publication, including legacy material
        // zero: swapping it preserves the old color fallback on its physical side.
        Document multi;
        const auto a = box(multi), b = box(multi, {4, 0, 0});
        const auto mat = createMaterial(multi, "Back only", {.8f, .1f, .2f});
        assignMaterial(multi, a, 5, mat, false, true);
        const auto first = multi.bodies().at(a), second = multi.bodies().at(b);
        auto command = reverse(a, 5);
        auto entities = command["entities"].toArray();
        entities.append(QJsonObject{{"body", QString::number(b)}, {"face", "5"}});
        command["entities"] = entities;
        executeBatch(multi, batch(multi, {command}));
        check(faceMaterials(*multi.bodies().at(a), 5) == MaterialSides{mat, 0} &&
                  dot(multi.bodies().at(b)->surface.normal(5), second->surface.normal(5)) < 0,
              "Multi-body Reverse swaps legacy fallback and explicit material");
        multi.undo();
        check(multi.bodies().at(a) == first && multi.bodies().at(b) == second,
              "One Undo restores all bodies");
        command["entities"] = QJsonArray{entities[0], entities[0]};
        const auto stable = encodeDocument(multi);
        rejects([&] { executeBatch(multi, batch(multi, {command})); }, "ORIENTATION_SELECTION");
        check(encodeDocument(multi) == stable, "Duplicate faces reject without partial reversal");
        Document curved;
        curved.addCurve(
            0, centerCurve(CurveKind::Circle, DrawingPlane{}, 2, 0, 2 * std::numbers::pi, 24));
        const auto curvedId = curved.bodies().begin()->first;
        const auto face = curved.bodies().at(curvedId)->surface.faces.begin()->first;
        const auto curves = curved.bodies().at(curvedId)->curves;
        executeBatch(curved, batch(curved, {reverse(curvedId, face)}));
        check(curved.bodies().at(curvedId)->curves == curves,
              "Face reversal preserves analytic curve bindings");
        Document scoped;
        const auto raw = box(scoped), root = createGroup(scoped, {raw});
        const auto definition = createComponent(scoped, root).definition;
        const auto instance =
            placeComponent(scoped, definition,
                           Transform::translation({10, 0, 0}) * Transform::scaling({-1, 1, 1}))
                .instance;
        const auto member = scoped.instances().at(instance)->members.at(raw);
        const auto originalDefinition = scoped.definitions().at(definition);
        const auto sibling = scoped.bodies().at(raw);
        const auto scopedRequest =
            batch(scoped, {QJsonObject{{"command", "component.edit_instance"},
                                       {"body", QString::number(instance)},
                                       {"commands", QJsonArray{reverse(member, 5, instance)}}}});
        const auto before = encodeDocument(scoped);
        const auto preview = previewBatch(scoped, scopedRequest);
        check(encodeDocument(scoped) == before, "Unique-component preview is isolated");
        const auto result = executeBatch(scoped, scopedRequest);
        check(result["changes"] == preview["changes"] &&
                  scoped.definitions().at(definition) == originalDefinition &&
                  scoped.bodies().at(raw) == sibling &&
                  dot(scoped.bodies().at(member)->surface.normal(5), sibling->surface.normal(5)) <
                      0,
              "Unique-component Reverse changes scene member while preserving sibling definition");
        const auto reference = sibling->surface.faces.rbegin()->first;
        const auto ownDefinition = scoped.instances().at(instance)->definition;
        executeBatch(
            scoped,
            batch(scoped,
                  {QJsonObject{{"command", "component.edit"},
                               {"definition", QString::number(ownDefinition)},
                               {"instance", QString::number(instance)},
                               {"commands", QJsonArray{orient(member, reference, instance)}}}}));
        check(scoped.bodies().at(member)->surface == sibling->surface,
              "Scoped Orient maps reference identity and repairs connected member");
        auto reopened = decodeContainer(encodeContainer(scoped));
        check(encodeDocument(reopened) == encodeDocument(scoped),
              "Scoped orientation persists exactly");
        scoped.undo();
        check(dot(scoped.bodies().at(member)->surface.normal(5), sibling->surface.normal(5)) < 0,
              "Scoped Orient Undo restores reversed member");
        scoped.redo();
        check(scoped.bodies().at(member)->surface == sibling->surface,
              "Scoped Orient Redo restores repair");
        std::cout << "Orientation commands: scope, preview, physical material sides, curves, "
                     "rollback, Undo/persistence and components passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
