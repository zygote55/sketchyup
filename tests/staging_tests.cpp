#include "automation/commands.hpp"
#include "automation/staging.hpp"
#include "core/assets.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(const char *code, F operation) {
    try {
        operation();
    } catch (const InspectionError &e) {
        check(e.code() == code, e.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands},
            {"history", QJsonObject{{"label", "Move selected window"},
                                    {"assistant", true},
                                    {"taskId", "task-fixture"},
                                    {"request", "Move Window A and use meters"}}}};
}
QJsonObject query(const Document &doc, QString name, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["documentId"] = QString::fromStdString(doc.identity());
    fields["expectedRevision"] = QString::number(doc.revision());
    fields["query"] = name;
    return fields;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        auto live = loadDocument(QStringLiteral(SOURCE_DIR "/examples/m4-room-study.sketchyup"));
        const auto original = encodeContainer(live);
        const auto stamp = live.saveStamp();
        const auto rev = live.revision();
        StagingSession::Clock::time_point now{};
        bool expireOnRead = false;
        int clockReads = 0;
        StagingSession session({}, [&] {
            if (expireOnRead && ++clockReads == 2)
                now += std::chrono::seconds(301);
            return now;
        });
        const auto request =
            batch(live, {QJsonObject{{"command", "geometry.translate"},
                                     {"body", "7"},
                                     {"delta", QJsonArray{.5, 0, 0}}},
                         QJsonObject{{"command", "document.units"}, {"units", "m"}}});
        auto staged = session.prepare(live, request);
        const auto token = staged["stageId"].toString();
        check(staged["status"] == "staged" && staged["provisionalIds"] == true &&
                  staged["baseRevision"] == QString::number(rev) &&
                  staged["proposedRevision"] == QString::number(rev + 1) &&
                  staged["changeCount"] == 2 && session.retainedCount() == 1,
              "Preparation must report provisional results and a single composed revision");
        check(encodeContainer(live) == original && live.isCurrentSnapshot(stamp) && !live.canUndo(),
              "Private batch cannot change the live file or history");
        const auto proposal = session.proposal(live, token);
        const auto &preview = proposal->snapshot();
        check(preview.displayUnits() == DisplayUnit::Meters &&
                  live.displayUnits() == DisplayUnit::Millimeters &&
                  length(preview.worldTransform(7).point({0, 0, 0}) -
                         live.worldTransform(7).point({0, 0, 0}) - Vec3{.5, 0, 0}) < tolerance,
              "Preview must contain exact proposed geometry and units");
        const auto measured = session.inspect(
            live, token,
            query(preview, "measure.entity",
                  {{"target", inspectionReference(preview, 7)}, {"space", "local"}}));
        check(measured["status"] == "staged" && measured["stageId"] == token &&
                  !measured.contains("document"),
              "Staged bounded inspection must be explicitly identified");
        const auto first = session.changes(live, token, 0, 1);
        const auto second = session.changes(live, token, 1, 1);
        check(first["changes"].toArray().size() == 1 && first["nextOffset"] == 1 &&
                  first["changes"].toArray()[0].toObject()["kind"] == "body" &&
                  second["changes"].toArray()[0].toObject()["kind"] == "displayUnits" &&
                  second["nextOffset"].isNull(),
              "Direct record changes must paginate deterministically");
        rejects("INVALID_REQUEST", [&] { session.changes(live, token, 3, 1); });
        rejects("INVALID_REQUEST", [&] { session.changes(live, token, 0, 101); });
        live.markSaved();
        check(session.describe(live, token)["status"] == "staged",
              "Save must not invalidate content preview");
        auto candidate = live;
        candidate.applyPrepared(*proposal);
        const auto entry = candidate.history().entries.back();
        check(entry.label == "Move selected window" && entry.metadata.assistant &&
                  entry.metadata.taskId == "task-fixture" && candidate.history().total == 1 &&
                  candidate.revision() == rev + 1 &&
                  encodeContainer(candidate) == encodeContainer(preview),
              "Publication must match preview exactly and retain one assistant undo entry");
        candidate.undo();
        check(!candidate.dirty(), "Undo returns to the saved baseline");
        live.move(8, {0, .25, 0});
        rejects("STALE_PROPOSAL", [&] { session.proposal(live, token); });
        rejects("STALE_PROPOSAL",
                [&] { session.inspect(live, token, query(preview, "document.describe")); });
        live.undo();
        rejects("STALE_PROPOSAL", [&] { session.describe(live, token); });
        session.release(token);
        session.release(token);
        check(!session.retainedCount() && !session.retainedBytes(),
              "Release is idempotent and reclaims storage");
        const auto currentRequest = batch(live, {QJsonObject{{"command", "geometry.translate"},
                                                             {"body", "7"},
                                                             {"delta", QJsonArray{.5, 0, 0}}}});
        for (int i = 0; i < 4; ++i)
            session.prepare(live, currentRequest);
        rejects("STAGE_LIMIT", [&] { session.prepare(live, currentRequest); });
        now += std::chrono::seconds(61);
        const auto fresh = session.prepare(live, currentRequest, 1);
        check(session.retainedCount() == 1,
              "Admission must reclaim expired proposals without evicting live ones");
        now += std::chrono::seconds(1);
        rejects("STAGE_UNAVAILABLE", [&] { session.describe(live, fresh["stageId"].toString()); });
        check(!session.retainedCount() && !session.retainedBytes(),
              "Expiry reclaims retained bytes");
        const auto beforeQuery = session.prepare(live, currentRequest);
        const auto pinned = session.proposal(live, beforeQuery["stageId"].toString());
        expireOnRead = true;
        clockReads = 0;
        rejects("STAGE_UNAVAILABLE", [&] {
            session.inspect(live, beforeQuery["stageId"].toString(),
                            query(pinned->snapshot(), "document.describe"));
        });
        expireOnRead = false;
        check(!session.retainedCount(), "Expiry during inspection must reject and retire result");
        auto reopened = decodeContainer(encodeContainer(live));
        const auto bound = session.prepare(live, currentRequest);
        rejects("STALE_PROPOSAL", [&] { session.proposal(reopened, bound["stageId"].toString()); });
        auto wrong = currentRequest;
        wrong["documentId"] = "foreign";
        rejects("WRONG_DOCUMENT", [&] { session.prepare(live, wrong); });
        rejects("STALE_REVISION", [&] { session.prepare(live, request); });
        rejects("INVALID_REQUEST", [&] { session.prepare(live, currentRequest, 301); });
        auto oversized = currentRequest;
        oversized["padding"] = QString(65536, 'x');
        rejects("LIMIT_EXCEEDED", [&] { session.prepare(live, oversized); });
        const auto bytes = encodeContainer(live);
        const auto count = session.retainedCount();
        bool invalid = false;
        try {
            session.prepare(live, batch(live, {QJsonObject{{"command", "geometry.translate"},
                                                           {"body", "7"},
                                                           {"delta", QJsonArray{1, 0, 0}}},
                                               QJsonObject{{"command", "not.available"}}}));
        } catch (const std::runtime_error &) {
            invalid = true;
        }
        check(invalid && encodeContainer(live) == bytes && session.retainedCount() == count,
              "Failure after a private command cannot publish or retain partial work");
        Document resource;
        createAsset(
            resource, "Payload", "application/octet-stream",
            std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(2 * 1024 * 1024, 17)));
        StagingSession small({4, 1024 * 1024});
        rejects("STAGE_LIMIT", [&] {
            small.prepare(resource, batch(resource, {QJsonObject{{"command", "document.units"},
                                                                 {"units", "mm"}}}));
        });
        check(!small.retainedBytes() && !small.retainedCount(),
              "Owned resources must be charged before staging");
        Document empty;
        QJsonArray faces;
        for (int i = 0; i < 100; ++i)
            faces.append(
                QJsonObject{{"command", "geometry.face"},
                            {"name", QString::number(i)},
                            {"loops", QJsonArray{QJsonArray{QJsonArray{double(i * 2), 0, 0},
                                                            QJsonArray{double(i * 2 + 1), 0, 0},
                                                            QJsonArray{double(i * 2 + 1), 1, 0},
                                                            QJsonArray{double(i * 2), 1, 0}}}}});
        StagingSession growthBound({4, empty.readSnapshotBytes() * 4});
        rejects("STAGE_LIMIT", [&] { growthBound.prepare(empty, batch(empty, faces)); });
        check(empty.bodies().empty() && !growthBound.retainedCount(),
              "A proposal growing beyond its final charge must fail without publication");
        StagingSession many;
        const auto manyResult = many.prepare(empty, batch(empty, faces));
        const auto manyId = manyResult["stageId"].toString();
        check(manyResult["createdIds"].toObject()["created"].toArray().size() == 100 &&
                  many.changes(empty, manyId, 0, 60)["changes"].toArray().size() == 60 &&
                  many.changes(empty, manyId, 60, 60)["changes"].toArray().size() == 40,
              "Created IDs and paged direct diffs must cover the complete proposed batch");
        StagingSession duringPreparation({}, [&] {
            now += std::chrono::seconds(301);
            return now;
        });
        rejects("STAGE_UNAVAILABLE",
                [&] { duringPreparation.prepare(empty, batch(empty, faces)); });
        check(!duringPreparation.retainedCount(),
              "Preparation finishing after TTL cannot be retained");
        session.clear();
        check(!session.retainedBytes() && !session.retainedCount(),
              "Clear reclaims all staged records");
        std::cout << "Private batches, bounded previews/diffs, expiry, budgets and session guards "
                     "passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
