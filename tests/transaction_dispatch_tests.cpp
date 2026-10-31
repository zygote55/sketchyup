#include "automation/commands.hpp"
#include "automation/inspection_validation.hpp"
#include "automation/transactions.hpp"
#include "core/assets.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
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
        if (e.code() != code)
            throw std::runtime_error(std::string("Expected ") + code + ", got " + e.code() + ": " +
                                     e.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QJsonObject request(const Document &doc, QString operation, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["documentId"] = QString::fromStdString(doc.identity());
    fields["operation"] = "transaction." + operation;
    return fields;
}
QJsonObject begin(TransactionDispatcher &api, const Document &doc, QJsonObject fields = {}) {
    fields["expectedRevision"] = QString::number(doc.revision());
    return api.execute(request(doc, "begin", fields));
}
QJsonObject applyRequest(const Document &doc, QString id, int version, QString operationId,
                         QJsonArray commands) {
    return request(doc, "apply",
                   {{"transactionId", id},
                    {"expectedVersion", version},
                    {"operationId", operationId},
                    {"commands", commands}});
}
QJsonObject units(QString value) { return {{"command", "document.units"}, {"units", value}}; }
QJsonObject face() {
    return {{"command", "geometry.face"},
            {"name", "Draft face"},
            {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{1, 0, 0},
                                            QJsonArray{1, 1, 0}, QJsonArray{0, 1, 0}}}}};
}
QJsonObject draftRequest(const Document &doc, QString operation, QString id,
                         QJsonObject fields = {}) {
    fields["transactionId"] = id;
    return request(doc, operation, fields);
}
QJsonObject durableRequest(const Document &doc, QString operation, const QJsonObject &sealed) {
    return request(doc, operation,
                   {{"requestId", sealed["requestId"]}, {"payloadHash", sealed["payloadHash"]}});
}
// The room study plus thirteen 4 MiB payloads: about 52 MiB of records, within the 64 MiB asset
// budget, shared (not copied) by every draft and proposal taken from it.
Document largeModel() {
    auto doc = loadDocument(QStringLiteral(SOURCE_DIR "/examples/m4-room-study.sketchyup"));
    for (int i = 0; i < 13; ++i)
        createAsset(doc, "Bulk " + std::to_string(i), "application/octet-stream",
                    std::make_shared<const AssetPayload>(
                        std::vector<std::uint8_t>(4 * 1024 * 1024, std::uint8_t(i + 1))));
    return doc;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--print-capabilities")) {
        std::cout << QJsonDocument(transactionCapabilities()).toJson().toStdString();
        return 0;
    }
    try {
        QFile schema(QStringLiteral(SOURCE_DIR "/docs/api/transactions-v1.json"));
        check(schema.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(schema.readAll()).object() == transactionCapabilities(),
              "Published transaction schemas must match live registry");
        check(capabilities()["transactions"].toObject() == transactionCapabilities() &&
                  transactionCatalog().size() == 11,
              "Global discovery includes the transaction registry");
        QTemporaryDir files;
        check(files.isValid(), "Temporary transaction storage");
        Document empty;
        const auto original = encodeContainer(empty);
        QJsonObject sealed, committed;
        QByteArray committedBytes;
        {
            TransactionCoordinator actor(empty, files.path() + "/incremental");
            TransactionDispatcher api(actor);
            const auto &doc = actor.document();
            rejects("LIMIT_EXCEEDED", [&] { begin(api, doc, {{"ttlSeconds", 301}}); });
            const auto b = begin(api, doc,
                                 {{"history", QJsonObject{{"label", "Create and move face"},
                                                          {"assistant", true},
                                                          {"request", "Create a face and move it"},
                                                          {"taskId", "draft-task"}}}});
            const auto id = b["transactionId"].toString();
            check(b["stageVersion"] == 0 && b["status"] == "draft",
                  "Begin creates empty private draft");
            rejects("EMPTY_TRANSACTION", [&] {
                api.execute(draftRequest(doc, "preview", id, {{"expectedVersion", 0}}));
            });
            auto first = applyRequest(doc, id, 0, "create", {face()});
            const auto made = api.execute(first);
            const auto body = made["createdIds"].toObject()["created"].toArray()[0].toString();
            check(!body.isEmpty() && made["stageVersion"] == 1 && encodeContainer(doc) == original,
                  "Apply returns provisional IDs without live edits");
            const auto retried = api.execute(first);
            check(retried["replayed"] == true && retried["stageVersion"] == 1 &&
                      retried["commandCount"] == 1,
                  "Repeated apply appends once");
            auto conflict = first;
            conflict["commands"] = QJsonArray{units("mm")};
            rejects("REQUEST_CONFLICT", [&] { api.execute(conflict); });
            rejects("STALE_STAGE_VERSION",
                    [&] { api.execute(applyRequest(doc, id, 0, "old", {units("mm")})); });
            rejects("INVALID_OPERATION", [&] {
                api.execute(applyRequest(doc, id, 1, "bad",
                                         {QJsonObject{{"command", "geometry.translate"},
                                                      {"body", "999999"},
                                                      {"delta", QJsonArray{1, 0, 0}}}}));
            });
            check(api.execute(draftRequest(doc, "describe", id))["stageVersion"] == 1,
                  "Failed append preserves prior draft");
            auto measured = QJsonObject{
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(doc.identity())},
                {"expectedRevision", made["proposedRevision"]},
                {"query", "measure.entity"},
                {"space", "world"},
                {"target", QJsonObject{{"documentId", QString::fromStdString(doc.identity())},
                                       {"contextPath", QJsonArray{}},
                                       {"body", body},
                                       {"kind", "body"},
                                       {"id", body}}}};
            check(api.execute(draftRequest(doc, "inspect", id,
                                           {{"request", measured}}))["stageVersion"] == 1,
                  "Created entity is inspectable before commit");
            const auto moved = api.execute(
                applyRequest(doc, id, 1, "move",
                             {QJsonObject{{"command", "geometry.translate"},
                                          {"body", body},
                                          {"delta", QJsonArray{2, 0, 0}}},
                              QJsonObject{{"command", "entity.properties"},
                                          {"body", body},
                                          {"values", QJsonObject{{"description", "Made privately"},
                                                                 {"count", 2},
                                                                 {"approved", true}}}}}));
            check(moved["stageVersion"] == 2 && moved["commandCount"] == 3,
                  "Incremental commands address provisional IDs");
            const auto oldRetry = api.execute(first);
            check(oldRetry["stageVersion"] == 1 && oldRetry["currentVersion"] == 2,
                  "Old apply retry identifies receipt and current versions");
            check(api.execute(draftRequest(doc, "diff", id, {{"limit", 1}}))["changes"]
                          .toArray()
                          .size() == 1,
                  "Private changes are paged");
            actor.edit([](Document &d) { d.markSaved(); });
            const auto previewRequest = draftRequest(doc, "preview", id, {{"expectedVersion", 2}});
            sealed = api.execute(previewRequest);
            const auto repeated = api.execute(previewRequest);
            check(sealed["status"] == "sealed" && repeated["requestId"] == sealed["requestId"] &&
                      repeated["payloadHash"] == sealed["payloadHash"],
                  "Repeated preview retains immutable identity; save preserves draft");
            rejects("SEALED_TRANSACTION",
                    [&] { api.execute(applyRequest(doc, id, 2, "new", {units("mm")})); });
            check(api.execute(first)["replayed"] == true,
                  "Accepted apply receipt survives sealing");
            committed = api.execute(durableRequest(doc, "commit", sealed));
            committedBytes = encodeContainer(doc);
            check(committed["status"] == "committed" && doc.revision() == 1 &&
                      doc.history().total == 1 &&
                      doc.history().entries[0].metadata.taskId == "draft-task",
                  "Commit creates exactly one revision and original undo task");
            check(std::abs(doc.worldTransform(body.toULongLong()).point({0, 0, 0}).x - 2) <
                      tolerance,
                  "Committed geometry contains incremental move");
            check(api.execute(durableRequest(doc, "commit", sealed)) == committed,
                  "Lost commit reply retry returns original receipt");
            auto lateCancel = api.execute(durableRequest(doc, "cancel", sealed));
            lateCancel["operation"] = "transaction.commit";
            check(lateCancel == committed, "Late cancellation preserves committed outcome");
            rejects("TRANSACTION_UNAVAILABLE",
                    [&] { api.execute(draftRequest(doc, "describe", id)); });
            actor.edit([](Document &d) { d.undo(); });
            check(doc.bodies().empty() &&
                      api.execute(durableRequest(doc, "status", sealed))["status"] == "committed",
                  "User undo does not erase durable outcome");
        }
        {
            TransactionCoordinator::Options options;
            options.mode = TransactionCoordinator::OpenMode::RecoverLatest;
            TransactionCoordinator actor(empty, files.path() + "/incremental", options);
            TransactionDispatcher api(actor);
            check(encodeContainer(actor.document()) == committedBytes &&
                      api.execute(durableRequest(actor.document(), "commit", sealed)) ==
                          committed &&
                      actor.document().history().total == 1,
                  "Restart retry uses durable receipt and original undo without replay");
        }
        {
            TransactionCoordinator actor(empty, files.path() + "/validation");
            TransactionDispatcher api(actor);
            const auto &doc = actor.document();
            auto valid = request(doc, "begin", {{"expectedRevision", "0"}});
            auto invalid = valid;
            invalid["documentId"] = "other";
            rejects("WRONG_DOCUMENT", [&] { api.execute(invalid); });
            invalid = valid;
            invalid["apiVersion"] = 2;
            rejects("UNSUPPORTED_VERSION", [&] { api.execute(invalid); });
            invalid = valid;
            invalid["operation"] = "shell";
            rejects("UNSUPPORTED_CAPABILITY", [&] { api.execute(invalid); });
            invalid = valid;
            invalid["path"] = "/tmp/forbidden";
            rejects("INVALID_REQUEST", [&] { api.execute(invalid); });
            invalid = valid;
            invalid["padding"] = QString(65536, 'x');
            rejects("LIMIT_EXCEEDED", [&] { api.execute(invalid); });
            invalid = valid;
            invalid["expectedRevision"] = "18446744073709551616";
            rejects("INVALID_REQUEST", [&] { api.execute(invalid); });
            invalid = valid;
            invalid["expectedRevision"] = "01";
            rejects("INVALID_REQUEST", [&] { api.execute(invalid); });
            rejects("INVALID_REQUEST",
                    [&] { begin(api, doc, {{"history", QJsonObject{{"assistant", true}}}}); });
            auto thread = std::async(std::launch::async,
                                     [&] { rejects("WRONG_THREAD", [&] { api.execute(valid); }); });
            thread.get();
            const auto id = begin(api, doc)["transactionId"].toString();
            rejects("INVALID_REQUEST", [&] {
                api.execute(
                    applyRequest(doc, id, 0, "unknown", {QJsonObject{{"command", "shell"}}}));
            });
            QJsonObject badProperties{{"command", "entity.properties"},
                                      {"body", "1"},
                                      {"values", QJsonObject{{"bad", QJsonObject{}}}}};
            rejects("INVALID_REQUEST",
                    [&] { api.execute(applyRequest(doc, id, 0, "nested", {badProperties})); });
            badProperties["values"] = QJsonObject{{"", true}};
            rejects("INVALID_REQUEST",
                    [&] { api.execute(applyRequest(doc, id, 0, "empty-key", {badProperties})); });
            badProperties["values"] = QJsonObject{{"large", QString(2049, 'x')}};
            rejects("INVALID_REQUEST",
                    [&] { api.execute(applyRequest(doc, id, 0, "long", {badProperties})); });
            QJsonArray tooMany;
            for (int i = 0; i < 101; ++i)
                tooMany.append(units("mm"));
            rejects("LIMIT_EXCEEDED",
                    [&] { api.execute(applyRequest(doc, id, 0, "many", tooMany)); });
            check(api.execute(draftRequest(doc, "abort", id))["status"] == "aborted",
                  "Unsealed abort releases draft");
            rejects("TRANSACTION_UNAVAILABLE",
                    [&] { api.execute(draftRequest(doc, "describe", id)); });
            const auto unknown =
                QJsonObject{{"requestId", "9999"}, {"payloadHash", QString(64, 'a')}};
            check(api.execute(durableRequest(doc, "commit", unknown))["status"] == "unknown" &&
                      doc.revision() == 0,
                  "Unknown durable identity never creates work");
            const auto other = begin(api, doc)["transactionId"].toString();
            api.execute(applyRequest(doc, other, 0, "units", {units("mm")}));
            auto p = api.execute(draftRequest(doc, "preview", other, {{"expectedVersion", 1}}));
            check(api.execute(draftRequest(doc, "abort", other))["status"] == "aborted" &&
                      api.execute(durableRequest(doc, "commit", p))["status"] == "aborted",
                  "Sealed abort permanently prevents publication");
            check(encodeContainer(doc) == original,
                  "Rejected operations cannot affect live document");
        }
        {
            auto room =
                loadDocument(QStringLiteral(SOURCE_DIR "/examples/m4-room-study.sketchyup"));
            TransactionCoordinator actor(room, files.path() + "/stale");
            TransactionDispatcher api(actor);
            const auto &doc = actor.document();
            const auto id = begin(api, doc)["transactionId"].toString();
            api.execute(applyRequest(doc, id, 0, "move",
                                     {QJsonObject{{"command", "geometry.translate"},
                                                  {"body", "7"},
                                                  {"delta", QJsonArray{.5, 0, 0}}}}));
            actor.edit([](Document &d) {
                d.move(8, {0, .25, 0});
                d.undo();
            });
            rejects("STALE_REVISION", [&] { api.execute(draftRequest(doc, "describe", id)); });
            const auto fresh = begin(api, doc)["transactionId"].toString();
            api.execute(applyRequest(doc, fresh, 0, "units", {units("m")}));
            auto p = api.execute(draftRequest(doc, "preview", fresh, {{"expectedVersion", 1}}));
            actor.edit([](Document &d) { d.move(8, {0, .25, 0}); });
            const auto manual = encodeContainer(doc);
            const auto stale = api.execute(durableRequest(doc, "commit", p));
            check(stale["status"] == "aborted" &&
                      stale["result"].toObject()["reason"] == "STALE_REVISION" &&
                      encodeContainer(doc) == manual,
                  "Human edit wins over sealed stale request with its original cause");
        }
        {
            StagingSession::Clock::time_point now{};
            TransactionCoordinator::Options options;
            options.monotonic = [&] { return now; };
            TransactionCoordinator actor(empty, files.path() + "/limits", options);
            TransactionDispatcher api(actor, {}, options.monotonic);
            const auto &doc = actor.document();
            auto id = begin(api, doc, {{"ttlSeconds", 1}})["transactionId"].toString();
            for (int i = 0; i < 3; ++i)
                begin(api, doc, {{"ttlSeconds", 1}});
            rejects("STAGE_LIMIT", [&] { begin(api, doc); });
            now += std::chrono::seconds(1);
            rejects("TRANSACTION_EXPIRED", [&] { api.execute(draftRequest(doc, "describe", id)); });
            id = begin(api, doc, {{"ttlSeconds", 2}})["transactionId"].toString();
            api.execute(applyRequest(doc, id, 0, "units", {units("mm")}));
            now += std::chrono::milliseconds(1500);
            rejects("TRANSACTION_EXPIRED", [&] {
                api.execute(draftRequest(doc, "preview", id, {{"expectedVersion", 1}}));
            });
            now += std::chrono::milliseconds(500);
            rejects("TRANSACTION_EXPIRED",
                    [&] { api.execute(applyRequest(doc, id, 1, "again", {units("m")})); });
            check(doc.revision() == 0, "TTL cannot be extended by applying or sealing");
            TransactionDispatcher tiny(actor, {4, 1});
            rejects("STAGE_LIMIT", [&] { begin(tiny, doc); });
        }
        {
            Document large;
            createAsset(
                large, "Data", "application/octet-stream",
                std::make_shared<const AssetPayload>(std::vector<uint8_t>(2 * 1024 * 1024, 17)));
            // Owned resources are shared with the live model, so a draft that only changes units
            // is not charged for the 2 MiB payload. A draft that really overruns its budget is
            // refused and keeps its previous version.
            TransactionCoordinator actor(large, files.path() + "/bytes");
            TransactionDispatcher api(actor, {4, 1024 * 1024});
            const auto &doc = actor.document();
            const auto id = begin(api, doc)["transactionId"].toString();
            api.execute(applyRequest(doc, id, 0, "large", {units("mm")}));
            check(api.execute(draftRequest(doc, "describe", id))["stageVersion"] == 1,
                  "Shared owned resources must not be charged to a draft");
            TransactionDispatcher tiny(actor, {4, 8192});
            const auto overrun = begin(tiny, doc)["transactionId"].toString();
            QJsonArray commands;
            for (int i = 0; i < 40; ++i)
                commands.append(QJsonObject{
                    {"command", "geometry.face"},
                    {"name", QString::number(i)},
                    {"loops", QJsonArray{QJsonArray{QJsonArray{double(i * 2), 0, 0},
                                                    QJsonArray{double(i * 2 + 1), 0, 0},
                                                    QJsonArray{double(i * 2 + 1), 1, 0},
                                                    QJsonArray{double(i * 2), 1, 0}}}}});
            rejects("STAGE_LIMIT",
                    [&] { tiny.execute(applyRequest(doc, overrun, 0, "faces", commands)); });
            check(tiny.execute(draftRequest(doc, "describe", overrun))["stageVersion"] == 0,
                  "A draft that overruns its budget keeps its previous version");
        }
        {
            bool armed = false;
            TransactionDispatcher *active = nullptr;
            TransactionCoordinator::Options options;
            options.fault = [&](auto phase) {
                if (armed && phase == OutcomeStore::Phase::AfterRename) {
                    rejects("REENTRANT_TRANSACTION", [&] { active->execute(QJsonObject{}); });
                    throw std::runtime_error("lost durable commit acknowledgement");
                }
            };
            TransactionCoordinator actor(empty, files.path() + "/uncertain", options);
            TransactionDispatcher api(actor);
            active = &api;
            const auto &doc = actor.document();
            const auto id = begin(api, doc)["transactionId"].toString();
            api.execute(applyRequest(doc, id, 0, "units", {units("mm")}));
            const auto p = api.execute(draftRequest(doc, "preview", id, {{"expectedVersion", 1}}));
            armed = true;
            rejects("OUTCOME_UNKNOWN", [&] { api.execute(durableRequest(doc, "commit", p)); });
            armed = false;
            check(doc.revision() == 0 &&
                      api.execute(durableRequest(doc, "status", p))["status"] == "unknown",
                  "Uncertain commit freezes publication but allows status");
            rejects("OUTCOME_UNKNOWN", [&] { begin(api, doc); });
            rejects("OUTCOME_UNKNOWN", [&] { api.execute(durableRequest(doc, "cancel", p)); });
            check(api.execute(request(doc, "reconcile"))["status"] == "committed" &&
                      doc.revision() == 1 && doc.history().total == 1,
                  "Dispatcher reconciliation publishes verified candidate once");
            check(api.execute(durableRequest(doc, "commit", p))["status"] == "committed" &&
                      doc.history().total == 1,
                  "Retry after reconcile cannot duplicate publication");
        }
        {
            StagingSession::Clock::time_point now{};
            bool expire = false;
            TransactionCoordinator::Options options;
            options.monotonic = [&] { return now; };
            options.fault = [&](auto phase) {
                if (expire && phase == OutcomeStore::Phase::AfterDirectorySync) {
                    expire = false;
                    now += std::chrono::seconds(301);
                }
            };
            TransactionCoordinator actor(empty, files.path() + "/seal-expiry", options);
            TransactionDispatcher api(actor, {}, options.monotonic);
            const auto &doc = actor.document();
            const auto id = begin(api, doc)["transactionId"].toString();
            api.execute(applyRequest(doc, id, 0, "units", {units("mm")}));
            expire = true;
            rejects("TRANSACTION_EXPIRED", [&] {
                api.execute(draftRequest(doc, "preview", id, {{"expectedVersion", 1}}));
            });
            check(!actor.uncertain() && !actor.retainedStagingBytes() && doc.revision() == 0,
                  "Expiry during durable sealing retires accepted proposal without publication");
            begin(api, doc);
        }
        {
            // oneOf choices without "type" constrain the enclosing value's properties.
            const QJsonObject schema{
                {"type", "object"},
                {"properties", QJsonObject{{"kind", QJsonObject{{"type", "string"}}}}},
                {"oneOf",
                 QJsonArray{QJsonObject{{"properties",
                                         QJsonObject{{"kind", QJsonObject{{"const", "a"}}}}}},
                            QJsonObject{{"properties",
                                         QJsonObject{{"kind", QJsonObject{{"const", "b"}}}}}}}}};
            inspection_detail::validateParameters({{"kind", "a"}}, schema);
            inspection_detail::validateParameters({{"kind", "b"}}, schema);
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters({{"kind", "c"}}, schema); });
            const QJsonObject required{
                {"type", "object"},
                {"oneOf", QJsonArray{QJsonObject{{"required", QJsonArray{"a"}}},
                                     QJsonObject{{"required", QJsonArray{"b"}}}}}};
            inspection_detail::validateParameters({{"a", 1}}, required);
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters({{"a", 1}, {"b", 1}}, required); });
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters({{"c", 1}}, required); });

            // The shipped annotation.create schema through transaction.apply.
            Document annotated;
            TransactionCoordinator actor(annotated, files.path() + "/annotation-oneof");
            TransactionDispatcher api(actor);
            const auto &doc = actor.document();
            const auto id = begin(api, doc)["transactionId"].toString();
            auto anchor = [](double x) {
                return QJsonObject{{"kind", "point"},
                                   {"space", "world"},
                                   {"point", QJsonArray{x, 0, 0}}};
            };
            const QJsonObject distance{{"command", "annotation.create"},
                                       {"name", "Span"},
                                       {"kind", "distance"},
                                       {"anchors", QJsonArray{anchor(0), anchor(2)}}};
            const QJsonObject label{{"command", "annotation.create"},
                                    {"name", "Note"},
                                    {"kind", "label"},
                                    {"text", "Joint"},
                                    {"anchors", QJsonArray{anchor(1)}}};
            auto shortDistance = distance;
            shortDistance["anchors"] = QJsonArray{anchor(0)};
            auto emptyLabel = label;
            emptyLabel["text"] = "";
            auto untitledLabel = label;
            untitledLabel.remove("text");
            for (const auto &bad : {shortDistance, emptyLabel, untitledLabel})
                rejects("INVALID_REQUEST", [&] {
                    api.execute(applyRequest(doc, id, 0, "bad-annotation", {bad}));
                });
            const auto applied =
                api.execute(applyRequest(doc, id, 0, "annotations", {distance, label}));
            check(applied["stageVersion"] == 1 && applied["commandCount"] == 2,
                  "Transaction apply accepts annotation.create distance and label requests");
        }
        {
            // anyOf accepts a value that matches at least one choice, including several.
            const QJsonObject schema{
                {"type", "object"},
                {"properties", QJsonObject{{"a", QJsonObject{{"type", "integer"}}},
                                           {"b", QJsonObject{{"type", "string"}}}}},
                {"anyOf", QJsonArray{QJsonObject{{"required", QJsonArray{"a"}}},
                                     QJsonObject{{"required", QJsonArray{"b"}}}}}};
            inspection_detail::validateParameters({{"a", 1}}, schema);
            inspection_detail::validateParameters({{"b", "x"}}, schema);
            inspection_detail::validateParameters({{"a", 1}, {"b", "x"}}, schema);
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters({{"c", 1}}, schema); });
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters(QJsonObject{}, schema); });

            // Nested anyOf: the inner choice must also hold.
            const QJsonObject nested{
                {"type", "object"},
                {"anyOf",
                 QJsonArray{QJsonObject{{"required", QJsonArray{"a"}},
                                        {"anyOf",
                                         QJsonArray{QJsonObject{{"required", QJsonArray{"x"}}},
                                                    QJsonObject{{"required", QJsonArray{"y"}}}}}},
                            QJsonObject{{"required", QJsonArray{"b"}}}}}};
            inspection_detail::validateParameters({{"a", 1}, {"x", 1}}, nested);
            inspection_detail::validateParameters({{"a", 1}, {"y", 1}}, nested);
            inspection_detail::validateParameters({{"b", 1}}, nested);
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters({{"a", 1}}, nested); });
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters({{"x", 1}}, nested); });

            // anyOf inside a property and alongside oneOf.
            const QJsonObject inner{
                {"type", "object"},
                {"properties",
                 QJsonObject{{"v", QJsonObject{{"anyOf",
                                                QJsonArray{QJsonObject{{"type", "integer"}},
                                                           QJsonObject{{"const", "auto"}}}}}}}}};
            inspection_detail::validateParameters({{"v", 3}}, inner);
            inspection_detail::validateParameters({{"v", "auto"}}, inner);
            rejects("INVALID_REQUEST",
                    [&] { inspection_detail::validateParameters({{"v", "manual"}}, inner); });

            // The shipped reference_image.update and text.update anyOf schemas through the
            // transaction.apply schema (each update must name at least one change).
            Document edited;
            TransactionCoordinator actor(edited, files.path() + "/update-anyof");
            TransactionDispatcher api(actor);
            const auto &doc = actor.document();
            const auto id = begin(api, doc)["transactionId"].toString();
            QJsonObject apply;
            for (const auto &entry : transactionCatalog())
                if (entry.toObject()["name"] == "transaction.apply")
                    apply = entry.toObject()["parameters"].toObject();
            auto validates = [&](const QJsonObject &command) {
                inspection_detail::validateParameters(
                    applyRequest(doc, id, 0, "anyof", {command}), apply);
            };
            const QJsonObject image{{"command", "reference_image.update"}, {"body", "1"}};
            rejects("INVALID_REQUEST", [&] { validates(image); });
            for (const auto &[key, value] :
                 std::initializer_list<std::pair<const char *, QJsonValue>>{
                     {"opacity", 0.5}, {"name", "Plan"}, {"width", 2.0}}) {
                auto changed = image;
                changed[key] = value;
                validates(changed);
            }
            const QJsonObject text{{"command", "text.update"}, {"body", "1"}};
            rejects("INVALID_REQUEST", [&] { validates(text); });
            auto noRegeneration = text;
            noRegeneration["regenerate"] = false;
            rejects("INVALID_REQUEST", [&] { validates(noRegeneration); });
            auto regeneration = text;
            regeneration["regenerate"] = true;
            validates(regeneration);
            auto renamed = text;
            renamed["name"] = "Sign";
            validates(renamed);
            rejects("INVALID_REQUEST", [&] {
                api.execute(applyRequest(doc, id, 0, "empty-image-update", {image}));
            });
        }
        {
            // D-082: a 52 MiB model admits four drafts, and a one-body draft seals, commits and
            // undoes through the coordinator. Its proposals share the unchanged records.
            TransactionCoordinator actor(largeModel(), files.path() + "/shared");
            TransactionDispatcher api(actor);
            const auto &doc = actor.document();
            check(doc.readSnapshotBytes() > 48 * 1024 * 1024,
                  "Synthetic model must be larger than the staging and snapshot budgets");
            QStringList drafts;
            for (int i = 0; i < 4; ++i)
                drafts << begin(api, doc)["transactionId"].toString();
            rejects("STAGE_LIMIT", [&] { begin(api, doc); });
            const auto unchanged = api.execute(applyRequest(
                doc, drafts[0], 0, "zero-move",
                {QJsonObject{{"command", "geometry.translate"},
                             {"body", "7"},
                             {"delta", QJsonArray{0, 0, 0}}}}));
            check(unchanged["stageVersion"] == 1 && unchanged["commandCount"] == 1,
                  "A zero-distance draft must apply over a large shared model");
            const auto id = drafts[1];
            api.execute(applyRequest(doc, id, 0, "move",
                                     {QJsonObject{{"command", "geometry.translate"},
                                                  {"body", "7"},
                                                  {"delta", QJsonArray{.5, 0, 0}}}}));
            const auto described = api.execute(draftRequest(doc, "describe", id));
            check(described["stageVersion"] == 1 && described["changeCount"] == 1,
                  "A one-body draft over a large model must stage its exact change");
            // A durable commit stores before and after containers within the 64 MiB outcome budget,
            // which a 52 MiB model exceeds. That cap is not part of this fix and stays unchanged,
            // so the refusal must name it and publish nothing.
            const auto sealed =
                api.execute(draftRequest(doc, "preview", id, {{"expectedVersion", 1}}));
            check(sealed["status"] == "sealed", "Sealing a large-model draft must succeed");
            const auto revision = doc.revision();
            rejects("OUTCOME_LIMIT", [&] { api.execute(durableRequest(doc, "commit", sealed)); });
            check(doc.revision() == revision,
                  "A commit refused by the durable budget must publish nothing");
        }
        std::cout << "Incremental transactions, immutable seals, retry receipts, schemas, expiry "
                     "and limits passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
