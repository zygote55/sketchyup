#include "automation/inspection_session.hpp"
#include "core/assets.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(const char *code, F f) {
    try {
        f();
    } catch (const InspectionError &e) {
        check(e.code() == code,
              (std::string("Unexpected code: ") + e.code() + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QJsonObject request(const Document &doc, QString query, QJsonObject args = {}) {
    args["apiVersion"] = 1;
    args["documentId"] = QString::fromStdString(doc.identity());
    args["expectedRevision"] = QString::number(doc.revision());
    args["query"] = query;
    return args;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        auto doc = loadDocument(QString(SOURCE_DIR) + "/examples/m4-room-study.sketchyup");
        const auto original = encodeContainer(doc);
        check(encodeContainer(doc.readSnapshot()) == original,
              "Read snapshot must retain the complete native model exactly");
        Id room{}, window{}, other{};
        for (const auto &[id, body] : doc.bodies()) {
            if (body->name == "Room study")
                room = id;
            if (body->name == "Window A")
                window = id;
            if (body->name == "Window B")
                other = id;
        }
        check(room && window && other, "Missing integrated room fixture targets");
        Selection editor;
        editor.enter(doc, room);
        editor.apply(doc, {{window, SelectionKind::Body, 0}}, SelectionMode::Replace);
        editor.hide(doc, {{other, SelectionKind::Body, 0}});
        editor.lock(doc, other, true);
        InspectionSession::Clock::time_point now{};
        bool expireDuringQuery{};
        int timeReads{};
        InspectionSession session({}, [&] {
            if (expireDuringQuery && ++timeReads > 1)
                return now + std::chrono::seconds(2);
            return now;
        });
        const auto captured =
            session.execute(doc, request(doc, "snapshot.begin", {{"ttlSeconds", 10}}), &editor);
        const auto id = captured["snapshotId"];
        check(session.retainedCount() == 1 && session.retainedBytes() >= doc.readSnapshotBytes(),
              "Snapshot retention accounting omitted current model records");
        const auto ref = inspectionReference(doc, window);
        auto measurement = request(doc, "measure.entity", {{"target", ref}, {"space", "world"}});
        const QJsonValue baseline = session.execute(doc, measurement, &editor)["data"];
        measurement["snapshotId"] = id;
        auto selection = request(doc, "selection.get", {{"snapshotId", id}});
        auto description = request(doc, "document.describe", {{"snapshotId", id}});
        auto hierarchy =
            request(doc, "entities.query", {{"snapshotId", id}, {"recursive", true}, {"limit", 1}});
        const auto first = session.execute(doc, hierarchy);
        hierarchy["cursor"] = first["data"].toObject()["nextCursor"];
        check(!hierarchy["cursor"].isNull(), "Fixture must require continuation");
        doc.move(window, {.5, 0, 0});
        doc.setDisplayUnits(DisplayUnit::FeetInches);
        renameEntity(doc, window, "Changed live window");
        editor.clear();
        editor.reveal(doc);
        editor.unlockAll(doc);
        const auto edited = encodeContainer(doc);
        const auto stamp = doc.saveStamp();
        const auto snapshotMeasurement = session.execute(doc, measurement, &editor);
        check(snapshotMeasurement["data"] == baseline &&
                  snapshotMeasurement["differsFromLiveRevision"] == true &&
                  snapshotMeasurement["liveRevision"] == QString::number(doc.revision()),
              "Captured geometry must stay exact while live edits advance");
        const auto selected = session.execute(doc, selection)["data"].toObject();
        check(selected["total"] == 1 &&
                  selected["items"].toArray()[0].toObject()["owner"].toObject()["name"] ==
                      "Window A",
              "Snapshot must retain the captured selection and metadata");
        const auto desc = session.execute(doc, description)["data"].toObject();
        check(desc["displayUnits"] == "mm" && desc["dirty"] == false &&
                  desc["counts"].toObject()["assets"] == 2,
              "Snapshot must retain units, saved state and native resource records");
        check(session.execute(doc, hierarchy)["data"].toObject()["items"].toArray().size() == 1,
              "Live edits and editor changes must not invalidate snapshot continuation");
        auto hidden = request(doc, "entity.describe",
                              {{"target", inspectionReference(doc, other)}, {"snapshotId", id}});
        hidden["expectedRevision"] = captured["revision"];
        const auto visibility =
            session.execute(doc, hidden)["data"].toObject()["visibility"].toObject();
        check(visibility["effectiveHidden"] == true && visibility["persistentHidden"] == false &&
                  visibility["locked"] == true,
              "Captured temporary hiding and locks must survive snapshot reconstruction");
        check(doc.isCurrentSnapshot(stamp) && encodeContainer(doc) == edited &&
                  editor.entities().empty(),
              "Snapshot reads must leave the authoritative model and editor alone");
        auto staleBegin = request(doc, "snapshot.begin");
        staleBegin["expectedRevision"] = captured["revision"];
        rejects("STALE_REVISION", [&] { session.execute(doc, staleBegin); });
        auto wrongRevision = measurement;
        wrongRevision["expectedRevision"] = QString::number(doc.revision());
        rejects("STALE_REVISION", [&] { session.execute(doc, wrongRevision); });
        auto bad = request(doc, "snapshot.begin", {{"ttlSeconds", 301}});
        rejects("LIMIT_EXCEEDED", [&] { session.execute(doc, bad); });
        bad = request(doc, "snapshot.begin", {{"ttlSeconds", 1.5}});
        rejects("INVALID_REQUEST", [&] { session.execute(doc, bad); });
        bad = request(doc, "snapshot.begin", {{"snapshotId", id}});
        rejects("INVALID_REQUEST", [&] { session.execute(doc, bad); });
        bad = request(doc, "snapshot.release");
        rejects("INVALID_REQUEST", [&] { session.execute(doc, bad); });
        bad = request(doc, "snapshot.begin");
        bad["documentId"] = "different";
        rejects("WRONG_DOCUMENT", [&] { session.execute(doc, bad); });
        const auto dirty = session.execute(doc, request(doc, "snapshot.begin"));
        auto dirtyQuery = request(doc, "document.describe", {{"snapshotId", dirty["snapshotId"]}});
        check(session.execute(doc, dirtyQuery)["data"].toObject()["dirty"] == true,
              "Capturing must not turn an unsaved model into an explicitly saved model");
        auto missingEditor = request(doc, "selection.get", {{"snapshotId", dirty["snapshotId"]}});
        rejects("UNAVAILABLE_CONTEXT", [&] { session.execute(doc, missingEditor, &editor); });
        // Supplying today's editor cannot fill in a snapshot that captured none.
        const auto third = session.execute(doc, request(doc, "snapshot.begin"));
        session.execute(doc, request(doc, "snapshot.begin"));
        rejects("SNAPSHOT_LIMIT", [&] { session.execute(doc, request(doc, "snapshot.begin")); });
        const auto bytes = session.retainedBytes();
        session.execute(doc,
                        request(doc, "snapshot.release", {{"snapshotId", third["snapshotId"]}}));
        check(session.retainedCount() == 3 && session.retainedBytes() < bytes,
              "Release must retire storage");
        rejects("SNAPSHOT_UNAVAILABLE", [&] {
            session.execute(
                doc, request(doc, "document.describe", {{"snapshotId", third["snapshotId"]}}));
        });
        now += std::chrono::seconds(11);
        rejects("SNAPSHOT_UNAVAILABLE", [&] { session.execute(doc, measurement); });
        check(session.retainedCount() == 2, "Only expired snapshots should retire");
        now += std::chrono::seconds(60);
        session.execute(doc, request(doc, "document.describe"));
        check(session.retainedCount() == 0 && session.retainedBytes() == 0,
              "Expired snapshot bytes must be reclaimed on dispatch");
        const auto shortLived =
            session.execute(doc, request(doc, "snapshot.begin", {{"ttlSeconds", 1}}));
        expireDuringQuery = true;
        timeReads = 0;
        rejects("SNAPSHOT_UNAVAILABLE", [&] {
            session.execute(
                doc, request(doc, "document.describe", {{"snapshotId", shortLived["snapshotId"]}}));
        });
        check(session.retainedCount() == 0,
              "Expiry during inspection must reject and reclaim the result");
        expireDuringQuery = false;

        Document resource;
        const auto payload =
            std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(2 * 1024 * 1024, 17));
        createAsset(resource, "Owned resource", "application/octet-stream", payload);
        InspectionSession resourceBound({4, 1024 * 1024});
        rejects("SNAPSHOT_LIMIT",
                [&] { resourceBound.execute(resource, request(resource, "snapshot.begin")); });
        check(resourceBound.retainedCount() == 0 &&
                  resource.readSnapshotBytes() > payload->bytes().size(),
              "Owned payloads must be charged before snapshot allocation");
        InspectionSession tiny({4, 1024});
        rejects("SNAPSHOT_LIMIT", [&] { tiny.execute(doc, request(doc, "snapshot.begin")); });
        check(tiny.retainedBytes() == 0 && tiny.retainedCount() == 0,
              "Failed capture must not retain partial storage");
        rejects("INVALID_REQUEST", [&] { InspectionSession invalid({5, 1024}); });
        const auto beforeReplace = session.execute(doc, request(doc, "snapshot.begin"));
        auto reopened = decodeContainer(edited);
        check(reopened.identity() == doc.identity() && reopened.revision() == doc.revision(),
              "Replacement fixture should share identity and revision");
        rejects("SNAPSHOT_UNAVAILABLE", [&] {
            session.execute(reopened, request(reopened, "document.describe",
                                              {{"snapshotId", beforeReplace["snapshotId"]}}));
        });
        check(session.retainedCount() == 0,
              "Replacing even the same file must invalidate old live-session captures");
        rejects("STALE_SELECTION",
                [&] { session.execute(reopened, request(reopened, "snapshot.begin"), &editor); });
        session.execute(doc, request(doc, "snapshot.begin"));
        session.clear();
        check(session.retainedCount() == 0 && session.retainedBytes() == 0,
              "Closing the client session must release all captures");
        QFile schemas(QString(SOURCE_DIR) + "/docs/api/inspection-session-v1.json");
        check(schemas.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(schemas.readAll()).object() ==
                      inspectionSessionCapabilities(),
              "Published session schema must match executable discovery");
        std::cout << "Immutable inspection snapshots, captured selection, expiry and retention "
                     "checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
