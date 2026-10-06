#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/inspection.hpp"
#include "automation/staging.hpp"
#include "core/components.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/selection.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include "io/scenes_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected saved scene command rejection");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject query(const Document &doc, const char *name, QJsonObject extra = {}) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = name;
    return extra;
}
QJsonObject command(const char *name, Id id) {
    return {{"command", name}, {"scene", QString::number(id)}};
}
QJsonArray pages(const Document &doc, const char *name, QJsonObject extra = {}) {
    auto request = query(doc, name, extra);
    request["limit"] = 1;
    QJsonArray rows;
    do {
        const auto result = inspectDocument(doc, request)["data"].toObject();
        check(result["items"].toArray().size() <= 1, "Bounded scene query respects page size");
        for (const auto &row : result["items"].toArray())
            rows.append(row);
        if (result["nextCursor"].isNull())
            break;
        request["cursor"] = result["nextCursor"];
    } while (true);
    return rows;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        const auto tag = createTag(doc, "Layer");
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        SceneSnapshot snapshot;
        snapshot.camera = SceneCamera{};
        snapshot.style = doc.style();
        snapshot.style->mode = ModelStyleMode::Wireframe;
        snapshot.visibility = SceneVisibility{
            {{body, false}}, {{tag, false}}, {{body, SceneEntityKind::Face, face}}, false};
        snapshot.section = SceneSection{};
        const QJsonObject create{{"command", "saved_scene.create"},
                                 {"name", "Presentation"},
                                 {"snapshot", encodeSceneSnapshot(snapshot)}};
        doc.markSaved();
        const auto original = encodeContainer(doc);
        const auto saved = doc.saveStamp();
        const auto request = batch(doc, {create});
        previewBatch(doc, request);
        check(encodeContainer(doc) == original, "Scene preview leaves original bytes");
        StagingSession staging;
        const auto result = staging.prepare(doc, request);
        const auto stage = result["stageId"].toString();
        const auto changes = staging.changes(doc, stage)["changes"].toArray();
        check(changes.size() == 1 && changes[0].toObject()["kind"] == "scene",
              "Scene stage names its resource");
        doc.applyPrepared(*staging.proposal(doc, stage));
        const auto id = doc.scenes().begin()->first;
        check(doc.scenes().at(id)->snapshot == snapshot,
              "Shared create records exact selective snapshot");
        doc.undo();
        check(doc.isCurrentSnapshot(saved), "Scene command Undo restores saved state");
        doc.redo();
        const auto info = inspectDocument(doc, query(doc, "saved_scene.describe",
                                                     {{"scene", QString::number(id)}}))["data"]
                              .toObject();
        check(info["snapshot"].toObject().contains("camera") &&
                  !info["snapshot"].toObject().contains("visibility") &&
                  info["visibility"].toObject()["bodies"] == 1,
              "Large visibility references queried separately");
        check(pages(doc, "saved_scenes.query").size() == 1 &&
                  pages(doc, "saved_scene.visibility", {{"scene", QString::number(id)}}).size() ==
                      3,
              "Scene and typed visibility pagination");
        const auto history = doc.history().total;
        const auto beforeRecall = encodeDocument(doc);
        executeBatch(doc, batch(doc, {command("saved_scene.recall", id)}));
        check(doc.style() == *snapshot.style && doc.bodies().at(body)->hidden &&
                  !doc.tags().at(tag)->visible && doc.history().total == history + 1,
              "Recall applies opted-in persistent state in one edit");
        doc.undo();
        check(doc.style() == ModelStyle{} && !doc.bodies().at(body)->hidden &&
                  doc.tags().at(tag)->visible,
              "Recall Undo restores model state");
        doc.redo();
        rejects([&] { executeBatch(doc, batch(doc, {command("saved_scene.recall", id)})); });
        auto second = create;
        second["name"] = "Second";
        const auto created = executeBatch(doc, batch(doc, {second}));
        const auto other = created["createdScenes"].toArray()[0].toString().toULongLong();
        check(other > id && doc.scenes().contains(other),
              "Created scene IDs are explicit in batch results");
        auto rename = command("saved_scene.rename", id);
        rename["name"] = "Renamed";
        executeBatch(doc,
                     batch(doc, {rename, QJsonObject{{"command", "saved_scene.reorder"},
                                                     {"order", QJsonArray{QString::number(other),
                                                                          QString::number(id)}}}}));
        check(doc.scenes().at(id)->name == "Renamed" && orderedScenes(doc).front() == other,
              "Compound rename and order share one publication");
        SceneSnapshot camera;
        camera.camera = SceneCamera{};
        auto update = command("saved_scene.update", id);
        update["snapshot"] = encodeSceneSnapshot(camera);
        executeBatch(doc, batch(doc, {update}));
        check(!doc.scenes().at(id)->snapshot.style, "Update explicitly changes property ownership");
        const auto component = createComponent(doc, body, "Scoped");
        rejects([&] {
            executeBatch(
                doc, batch(doc, {componentScopeCommand(doc, component.instance,
                                                       {command("saved_scene.recall", other)})}));
        });
        doc.undo();
        doc.erase(body);
        eraseTag(doc, tag);
        const auto records =
            pages(doc, "saved_scene.visibility", {{"scene", QString::number(other)}});
        for (const auto &record : records)
            check(record.toObject()["missing"] == true, "Every deleted typed ref is diagnosed");
        const auto summary =
            inspectDocument(doc, query(doc, "saved_scene.describe",
                                       {{"scene", QString::number(other)}}))["data"]
                .toObject();
        check(summary["missingReferences"].toObject()["bodies"] == 1,
              "Scene summary reports missing count");
        const auto before = encodeContainer(doc);
        for (int variant = 0; variant < 10; ++variant) {
            auto bad = create;
            auto state = encodeSceneSnapshot(snapshot);
            if (variant == 0)
                bad["name"] = 1;
            if (variant == 1)
                bad["snapshot"] = QJsonObject{};
            if (variant == 2)
                state["camera"] = QJsonValue::Null;
            if (variant == 3) {
                auto c = state["camera"].toObject();
                c["yaw"] = 181;
                state["camera"] = c;
            }
            if (variant == 4) {
                auto v = state["visibility"].toObject();
                auto a = v["bodies"].toArray();
                a.append(a[0]);
                v["bodies"] = a;
                state["visibility"] = v;
            }
            if (variant == 5) {
                auto v = state["visibility"].toObject();
                v["showHidden"] = 1;
                state["visibility"] = v;
            }
            if (variant == 6) {
                auto s = state["section"].toObject();
                s["plane"] = QJsonArray{0, 0, 2, 0};
                state["section"] = s;
            }
            if (variant == 7)
                bad["future"] = true;
            if (variant == 8)
                bad["name"] = " invalid";
            if (variant == 9)
                state["future"] = true;
            if (variant >= 2)
                bad["snapshot"] = state;
            rejects([&] { executeBatch(doc, batch(doc, {bad})); });
        }
        check(encodeContainer(doc) == before, "Invalid captures preserve all live bytes");
        rejects([&] {
            executeBatch(doc, batch(doc, {command("saved_scene.delete", id),
                                          QJsonObject{{"command", "unknown"}}}));
        });
        check(encodeContainer(doc) == before, "Mixed batch failure rolls scene deletion back");
        executeBatch(doc, batch(doc, {command("saved_scene.delete", id)}));
        doc.undo();
        check(doc.scenes().contains(id), "Shared delete Undo restores scene");
        const auto stale = query(doc, "saved_scenes.query");
        rename = command("saved_scene.rename", id);
        rename["name"] = "Final";
        executeBatch(doc, batch(doc, {rename}));
        rejects([&] { inspectDocument(doc, stale); });
        Document hiddenDocument;
        const auto a = hiddenDocument.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}});
        const auto b = hiddenDocument.addFace({{{2, 0, 0}, {3, 0, 0}, {3, 1, 0}}});
        Selection editor;
        editor.sync(hiddenDocument);
        editor.hide(hiddenDocument, {{a, SelectionKind::Body, 0}, {b, SelectionKind::Body, 0}});
        editor.lock(hiddenDocument, b, true);
        const auto beforeEditor = encodeContainer(hiddenDocument);
        SceneVisibility selective{
            {{a, true}, {999, true}}, {}, {{999, SceneEntityKind::Face, 7}}, true};
        editor.restoreSceneVisibility(hiddenDocument, selective);
        check(!editor.hiddenEntities().contains({a, SelectionKind::Body, 0}) &&
                  editor.hiddenEntities().contains({b, SelectionKind::Body, 0}) &&
                  editor.hiddenEntities().size() == 1 && editor.lockedBodies().contains(b) &&
                  editor.showingHidden() && encodeContainer(hiddenDocument) == beforeEditor,
              "Temporary recall preserves uncaptured hiding and locks, skips missing refs and "
              "leaves model bytes");
        Document sectionDocument;
        const auto plane = createSection(sectionDocument, "Root cut", 0, {});
        SceneSnapshot withSection;
        withSection.section = SceneSection{std::nullopt, {{0, plane}}};
        executeBatch(sectionDocument, batch(sectionDocument, {
            QJsonObject{{"command", "saved_scene.create"}, {"name", "Section snapshot"},
                        {"snapshot", encodeSceneSnapshot(withSection)}}}));
        const auto sceneId = sectionDocument.scenes().begin()->first;
        const auto description = inspectDocument(sectionDocument,
            query(sectionDocument, "saved_scene.describe", {{"scene", QString::number(sceneId)}}))
            ["data"].toObject();
        check(description["snapshot"].toObject()["section"].toObject()["active"].toArray().size() == 1,
              "Shared inspection exposes captured named section identities");
        executeBatch(sectionDocument, batch(sectionDocument, {command("saved_scene.recall", sceneId)}));
        check(sectionDocument.activeSections() == ActiveSections{{0, plane}},
              "Shared scene recall restores persisted section activation");
        sectionDocument.undo();
        check(sectionDocument.activeSections().empty(), "Shared section recall is undoable");
        eraseSection(sectionDocument, plane);
        const auto missingSection = inspectDocument(sectionDocument,
            query(sectionDocument, "saved_scene.describe", {{"scene", QString::number(sceneId)}}))
            ["data"].toObject();
        check(missingSection["missingReferences"].toObject()["sections"] == 1,
              "Shared scene inspection diagnoses deleted sections");
        std::cout << "Saved scene authoring, bounded queries, staging, recall, Undo, scope and "
                     "atomic rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
