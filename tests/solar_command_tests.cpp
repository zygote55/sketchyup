#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/staging.hpp"
#include "io/document_io.hpp"
#include "io/solar_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid solar command accepted");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(const SolarSettings &s) {
    return {{"command", "document.solar"}, {"solar", encodeSolarSettings(s)}};
}
QJsonObject query(const Document &doc) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"query", "solar.describe"}};
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        doc.markSaved();
        SolarSettings settings;
        settings.enabled = true;
        settings.latitude = 40;
        settings.longitude = -105;
        settings.time = {2010, 6, 21, 12, 0, 0, -420};
        const auto original = encodeContainer(doc);
        const auto request = batch(doc, {command(settings)});
        previewBatch(doc, request);
        check(encodeContainer(doc) == original, "Preview does not mutate solar state");
        StagingSession staging;
        const auto result = staging.prepare(doc, request);
        const auto token = result["stageId"].toString();
        const auto proposal = staging.proposal(doc, token);
        check(proposal->snapshot().solar() == settings && doc.solar() == SolarSettings{},
              "Private solar proposal is isolated");
        const auto changes = staging.changes(doc, token)["changes"].toArray();
        check(changes.size() == 1 && changes[0].toObject()["kind"] == "solar",
              "Solar stage reports a document-level change");
        doc.applyPrepared(*proposal);
        check(doc.solar() == settings && doc.dirty(), "Solar publication succeeds");
        auto inspect = query(doc);
        const auto frozen = encodeContainer(doc);
        const auto data = inspectDocument(doc, inspect)["data"].toObject();
        check(data["settings"] == encodeSolarSettings(settings) &&
                  data["position"] == describeSolarPosition(settings),
              "Inspection reports exact settings and deterministic sun");
        check(encodeContainer(doc) == frozen, "Solar inspection is read-only");
        check(describe(doc)["solar"] == encodeSolarSettings(settings),
              "Legacy document description includes sun study");
        doc.undo();
        check(doc.solar() == SolarSettings{} && !doc.dirty(),
              "One Undo restores prior study and save marker");
        doc.redo();
        rejects([&] { inspectDocument(doc, inspect); });
        const auto rev = doc.revision();
        rejects([&] { executeBatch(doc, batch(doc, {command(settings)})); });
        check(doc.revision() == rev, "Identical sun command creates no history");
        auto night = settings;
        night.time.hour = 0;
        const auto stable = encodeContainer(doc);
        rejects([&] {
            executeBatch(doc,
                         batch(doc, {command(night), QJsonObject{{"command", "saved_scene.recall"},
                                                                 {"scene", "99999"}}}));
        });
        check(encodeContainer(doc) == stable, "Later batch failure rolls back sun study");
        auto bad = encodeSolarSettings(settings);
        bad["month"] = 2;
        bad["day"] = 30;
        rejects([&] {
            executeBatch(doc,
                         batch(doc, {QJsonObject{{"command", "document.solar"}, {"solar", bad}}}));
        });
        check(encodeContainer(doc) == stable, "Invalid civil date rejects atomically");
        executeBatch(
            doc,
            batch(doc, {QJsonObject{
                           {"command", "saved_scene.create"},
                           {"name", "Noon"},
                           {"snapshot", QJsonObject{{"solar", encodeSolarSettings(settings)}}}}}));
        executeBatch(doc, batch(doc, {command(night)}));
        check(inspectDocument(doc, query(doc))["data"]
                      .toObject()["position"]
                      .toObject()["directLightActive"] == false,
              "Night reports no direct sunlight");
        executeBatch(doc,
                     batch(doc, {QJsonObject{{"command", "saved_scene.recall"}, {"scene", "1"}}}));
        check(doc.solar() == settings, "Shared scene recall restores sun study");
        doc.undo();
        check(doc.solar() == night, "Scene recall Undo restores night");
        const auto raw = encodeContainer(doc);
        check(encodeContainer(decodeContainer(raw)) == raw,
              "Commands and solar scenes roundtrip exactly");
        std::cout << "Shared solar commands, inspection, preview, staged edits, rollback and scene "
                     "recall passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
