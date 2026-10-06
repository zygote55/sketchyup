#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/inspection.hpp"
#include "automation/staging.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/sections.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
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
    throw std::runtime_error("Expected section command rejection");
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
QJsonArray pages(const Document &doc, const char *name, QJsonObject extra = {}) {
    auto request = query(doc, name, extra);
    request["limit"] = 1;
    QJsonArray result;
    do {
        const auto data = inspectDocument(doc, request)["data"].toObject();
        check(data["items"].toArray().size() <= 1, "Section page respects bound");
        for (const auto &item : data["items"].toArray())
            result.append(item);
        if (data["nextCursor"].isNull())
            break;
        request["cursor"] = data["nextCursor"];
    } while (true);
    return result;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto group = createGroup(doc, {body});
        const auto other = doc.addFace({{{3, 0, 0}, {4, 0, 0}, {3, 1, 0}}});
        doc.transform(group, Transform::translation({10, 20, 30}) * Transform::scaling({-2, 3, 1}));
        const auto geometry = doc.bodies();
        const QJsonObject create{
            {"command", "section.create"},
            {"name", "Cut"},
            {"context", QString::number(group)},
            {"space", "world"},
            {"plane", QJsonObject{{"normal", QJsonArray{-1, 0, 0}}, {"offset", 8}}}};
        const auto original = encodeContainer(doc);
        previewBatch(doc, batch(doc, {create}));
        check(encodeContainer(doc) == original, "Section preview is immutable");
        StagingSession staging;
        const auto stage = staging.prepare(doc, batch(doc, {create}));
        const auto stageId = stage["stageId"].toString();
        const auto changes = staging.changes(doc, stageId)["changes"].toArray();
        check(changes.size() == 1 && changes[0].toObject()["kind"] == "section",
              "Stage names section resource");
        doc.applyPrepared(*staging.proposal(doc, stageId));
        const Id section = doc.sections().begin()->first;
        check(doc.sections().at(section)->plane == SectionPlane{{1, 0, 0}, -1},
              "World authoring preserves reflected local retained side");
        const QJsonObject activate{{"command", "section.activate"},
                                   {"context", QString::number(group)},
                                   {"section", QString::number(section)}};
        executeBatch(doc, batch(doc, {activate}));
        check(effectiveSectionCuts(doc, body).size() == 1 &&
                  effectiveSectionCuts(doc, other).empty(),
              "Scoped activation leaves unrelated geometry unclipped");
        check(doc.bodies() == geometry, "Shared section commands do not modify geometry");
        auto root = create;
        root["name"] = "Global";
        root["context"] = "0";
        root["space"] = "local";
        const auto created = executeBatch(doc, batch(doc, {root}));
        const auto rootId = created["createdSections"].toArray()[0].toString();
        check(doc.sections().contains(rootId.toULongLong()), "Created section IDs are explicit");
        check(pages(doc, "sections.query").size() == 2 &&
                  pages(doc, "sections.query", {{"context", QString::number(group)}}).size() == 1 &&
                  pages(doc, "sections.effective", {{"body", QString::number(body)}}).size() == 1,
              "Bounded section discovery, filters and effective path");
        const auto detail =
            inspectDocument(doc, query(doc, "section.describe",
                                       {{"section", QString::number(section)}}))["data"]
                .toObject();
        check(detail["worldPlane"] == QJsonArray{-1, 0, 0, 8} && detail["active"] == true &&
                  detail["missingContext"] == false,
              "Inspection exposes exact world plane and state");
        auto update = create;
        update["command"] = "section.update";
        update["section"] = QString::number(section);
        update["name"] = "Updated";
        update["fill"] = false;
        update["edges"] = false;
        update["color"] = QJsonArray{.125, .25, .375};
        executeBatch(doc, batch(doc, {update}));
        check(!doc.sections().at(section)->fill && !doc.sections().at(section)->edges,
              "All display properties use shared atomic update");
        doc.undo();
        check(doc.sections().at(section)->name == "Cut" && doc.sections().at(section)->fill,
              "Update Undo restores complete record");
        auto off = activate;
        off["section"] = QJsonValue::Null;
        executeBatch(doc, batch(doc, {off}));
        check(doc.activeSections().empty(), "Explicit null deactivates context");
        rejects([&] { executeBatch(doc, batch(doc, {off})); });
        doc.undo();
        const auto before = encodeContainer(doc);
        for (int variant = 0; variant < 8; ++variant) {
            auto bad = create;
            bad["name"] = "Invalid";
            if (variant == 0)
                bad["plane"] = QJsonObject{{"normal", QJsonArray{0, 0, 2}}, {"offset", 0}};
            if (variant == 1)
                bad["plane"] = QJsonObject{{"normal", QJsonArray{0, 0, 1}}, {"offset", "0"}};
            if (variant == 2)
                bad["space"] = "screen";
            if (variant == 3)
                bad["context"] = "999";
            if (variant == 4)
                bad["fill"] = 1;
            if (variant == 5)
                bad["color"] = QJsonArray{1, 2, 3};
            if (variant == 6)
                bad["context"] = 0;
            if (variant == 7)
                bad["unknown"] = true;
            rejects([&] {
                executeBatch(
                    doc,
                    batch(doc,
                          {QJsonObject{{"command", "section.delete"}, {"section", rootId}}, bad}));
            });
            check(encodeContainer(doc) == before,
                  "Malformed compound section batch rolls back all changes");
        }
        const auto component = createComponent(doc, other, "Shared");
        rejects([&] {
            executeBatch(doc,
                         batch(doc, {componentScopeCommand(doc, component.instance, {activate})}));
        });
        doc.undo();
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "section.delete"},
                                                  {"section", QString::number(section)}}}));
        check(!doc.sections().contains(section) && !doc.activeSections().contains(group),
              "Deleting active section deactivates within same transaction");
        doc.undo();
        check(doc.sections().contains(section) && doc.activeSections().at(group) == section,
              "Delete Undo restores activation with record");
        std::cout << "Section commands, transforms, staging, inspection and rollback passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
