#include "automation/native_assistant_session.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(const char *code, F function) {
    try {
        function();
    } catch (const InspectionError &error) {
        check(error.code() == code, error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QJsonObject operation(const Document &doc, QString name, QJsonObject args = {}) {
    args["apiVersion"] = 1;
    args["documentId"] = QString::fromStdString(doc.identity());
    args["operation"] = name;
    return args;
}
QJsonObject query(const Document &doc, QString name, QJsonObject args = {}) {
    args = operation(doc, name, args);
    args.remove("operation");
    args["query"] = name;
    args["expectedRevision"] = QString::number(doc.revision());
    return args;
}
struct Fixture {
    Document doc;
    Id body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    Selection selection;
    Fixture() {
        doc.move(body, {0, 1, 0});
        selection.apply(doc, {{body, SelectionKind::Body, 0}}, SelectionMode::Replace);
    }
};
struct Driver {
    NativeAssistantSession &session;
    Document &doc;
    QJsonObject last;
    std::unique_ptr<AssistantTask> task;
    int serial{};
    Driver(NativeAssistantSession &session, Document &doc,
           QStringList commands = {"geometry.translate"}, int seconds = 180,
           AssistantTask::Now now = AssistantTask::Clock::now)
        : session(session), doc(doc) {
        auto backend = session.backend();
        auto original = backend.call;
        backend.call = [this, original](const QJsonObject &request) {
            last = original(request);
            return last;
        };
        AssistantTask::Options options;
        options.provider = "fixture";
        options.model = "native";
        options.prompt = "Move the selected face half a metre along X, then preview it.";
        options.allowedCommands = std::move(commands);
        options.limits.seconds = seconds;
        options.context = {query(doc, "selection.get")};
        task = std::make_unique<AssistantTask>(backend, options, std::move(now));
    }
    QJsonObject send(QString name, QJsonObject args) {
        const auto request = task->nextRequest();
        check(request.has_value(), "Native task issues provider request");
        AssistantReply reply;
        reply.inputTokens = 100;
        reply.outputTokens = 50;
        reply.calls.push_back({"call-" + QString::number(++serial), name, args});
        check(task->accept(request->value("attemptId").toString(), reply), "Native reply accepted");
        return last;
    }
    QJsonObject stage(Id body) {
        const auto draft = send("transaction.begin",
                                operation(doc, "transaction.begin",
                                          {{"expectedRevision", QString::number(doc.revision())}}));
        const auto id = draft.value("transactionId");
        send("transaction.apply",
             operation(doc, "transaction.apply",
                       {{"transactionId", id},
                        {"expectedVersion", 0},
                        {"operationId", "move-face"},
                        {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                            {"body", QString::number(body)},
                                                            {"delta", QJsonArray{.5, 0, 0}}}}}}));
        send("transaction.preview", operation(doc, "transaction.preview",
                                              {{"transactionId", id}, {"expectedVersion", 1}}));
        check(task->phase() == AssistantTask::Phase::PreviewReady, "Native draft is sealed");
        return task->result().value("preview").toObject();
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Native session test directory");
        for (const auto action : {"apply", "review-expire", "expire", "cancel", "stale"}) {
            Fixture fixture;
            auto &doc = fixture.doc;
            const auto original = encodeBodies(doc.bodies());
            const auto history = doc.history().total;
            auto now = AssistantTask::Clock::now();
            const auto start = now;
            const auto wallStart = QDateTime::currentDateTimeUtc();
            auto configuration = nativeAssistantTransactionOptions();
            configuration.monotonic = [&] { return now; };
            configuration.wallClock = [&] {
                return wallStart.addMSecs(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
            };
            NativeAssistantSession session(doc, files.path() + "/slow-" + action,
                                           &fixture.selection, {}, configuration);
            Driver driver(session, doc, {"geometry.translate"}, 900, [&] { return now; });
            now += std::chrono::seconds(30);
            const auto begun =
                driver.send("transaction.begin",
                            operation(doc, "transaction.begin",
                                      {{"expectedRevision", QString::number(doc.revision())}}));
            const auto draft = begun.value("transactionId");
            check(begun["expiresInMs"].toInteger() == 870000,
                  "Native draft lifetime is bounded by the original task deadline");
            driver.send("transaction.apply",
                        operation(doc, "transaction.apply",
                                  {{"transactionId", draft},
                                   {"expectedVersion", 0},
                                   {"operationId", "slow-edit"},
                                   {"commands",
                                    QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                           {"body", QString::number(fixture.body)},
                                                           {"delta", QJsonArray{.5, 0, 0}}}}}}));
            now = start + std::chrono::seconds(350);
            const auto inspected = driver.send(
                "transaction.inspect",
                operation(doc, "transaction.inspect",
                          {{"transactionId", draft},
                           {"request",
                            QJsonObject{{"apiVersion", 1},
                                        {"query", "document.describe"},
                                        {"documentId", QString::fromStdString(doc.identity())},
                                        {"expectedRevision", driver.last["proposedRevision"]}}}}));
            check(inspected.contains("data") && encodeBodies(doc.bodies()) == original,
                  "Private geometry remains inspectable beyond five minutes without publication");
            if (QString(action) == "apply" || QString(action) == "review-expire") {
                now = start + std::chrono::seconds(400);
                driver.send("transaction.preview",
                            operation(doc, "transaction.preview",
                                      {{"transactionId", draft}, {"expectedVersion", 1}}));
                check(driver.task->phase() == AssistantTask::Phase::PreviewReady,
                      "Slow task seals a native preview after the former deadline");
                now = start + std::chrono::seconds(QString(action) == "apply" ? 650 : 700);
                driver.task->apply();
                if (QString(action) == "review-expire") {
                    check(driver.task->result()["applied"] == false &&
                              encodeBodies(doc.bodies()) == original,
                          "An expired sealed review cannot publish even inside the task deadline");
                    continue;
                }
                check(driver.task->result()["applied"] == true &&
                          doc.history().total == history + 1,
                      "Host Apply succeeds inside the sealed preview's five-minute review window");
                doc.undo();
                check(encodeBodies(doc.bodies()) == original, "Slow task still has one-entry Undo");
            } else {
                const auto pending = driver.task->nextRequest();
                check(pending.has_value(), "Slow task can continue after five minutes");
                if (QString(action) == "expire") {
                    now = start + std::chrono::seconds(900);
                    check(!driver.task->accept(pending->value("attemptId").toString(), {}),
                          "Reply at the exact task deadline cannot execute tools");
                    check(driver.task->result()["error"].toObject()["code"] == "TIME_LIMIT" &&
                              driver.task->result()["error"]
                                  .toObject()["message"]
                                  .toString()
                                  .contains("900-second limit"),
                          "Extended task still expires at a fixed, diagnosed deadline");
                } else if (QString(action) == "cancel") {
                    driver.task->cancel();
                    check(driver.task->phase() == AssistantTask::Phase::Canceled,
                          "Cancel remains available throughout a long task");
                } else {
                    doc.move(fixture.body, {1, 0, 0});
                    driver.task->nextRequest();
                    check(driver.task->phase() == AssistantTask::Phase::Stale,
                          "Long task still rejects a changed public document");
                    doc.undo();
                }
                check(encodeBodies(doc.bodies()) == original &&
                          driver.task->result()["applied"] == false,
                      "Expired, canceled and stale long tasks never publish their draft");
                rejects("TRANSACTION_UNAVAILABLE", [&] {
                    session.execute(
                        operation(doc, "transaction.describe", {{"transactionId", draft}}));
                });
            }
        }
        {
            Fixture fixture;
            auto &doc = fixture.doc;
            const auto original = encodeBodies(doc.bodies());
            const auto history = doc.history().total;
            NativeAssistantSession session(doc, files.path() + "/face-receipts",
                                           &fixture.selection);
            Driver driver(session, doc, nativeAssistantCommands());
            const QJsonValue draft =
                driver.send("transaction.begin",
                            operation(doc, "transaction.begin",
                                      {{"expectedRevision",
                                        QString::number(doc.revision())}}))["transactionId"];
            QJsonArray profiles;
            for (int i = 0; i < 14; ++i) {
                const double x = 3.0 * i;
                profiles.append(QJsonObject{
                    {"command", "geometry.face"},
                    {"loops",
                     QJsonArray{QJsonArray{QJsonArray{x, 0, 0}, QJsonArray{x + 2, 0, 0},
                                           QJsonArray{x + 2, .02, 0}, QJsonArray{x, .02, 0}}}}});
            }
            const auto receipt =
                driver.send("transaction.apply", operation(doc, "transaction.apply",
                                                           {{"transactionId", draft},
                                                            {"expectedVersion", 0},
                                                            {"operationId", "profiles"},
                                                            {"commands", profiles}}));
            const auto faces = receipt["createdIds"].toObject()["createdFaces"].toArray();
            check(faces.size() == 14, "Each newly created profile supplies its face reference");
            QJsonArray extrusions;
            for (const auto &value : faces) {
                const auto ref = value.toObject();
                check(ref["kind"] == "face" &&
                          ref["documentId"] == QString::fromStdString(doc.identity()),
                      "Creation references are bound to this native document");
                extrusions.append(QJsonObject{{"command", "geometry.extrude_isolated"},
                                              {"body", ref["body"]},
                                              {"face", ref["id"]},
                                              {"distance", .1}});
            }
            driver.send("transaction.apply", operation(doc, "transaction.apply",
                                                       {{"transactionId", draft},
                                                        {"expectedVersion", 1},
                                                        {"operationId", "extrusions"},
                                                        {"commands", extrusions}}));
            driver.send(
                "transaction.apply",
                operation(
                    doc, "transaction.apply",
                    {{"transactionId", draft},
                     {"expectedVersion", 2},
                     {"operationId", "slats"},
                     {"commands",
                      QJsonArray{QJsonObject{
                          {"command", "geometry.array_selection"},
                          {"entities", QJsonArray{QJsonObject{{"body", faces[0].toObject()["body"]},
                                                              {"kind", "context"},
                                                              {"entity", "0"}}}},
                          {"mode", "linear"},
                          {"count", 11},
                          {"delta", QJsonArray{0, 0, .15}}}}}}));
            driver.send("transaction.preview",
                        operation(doc, "transaction.preview",
                                  {{"transactionId", draft}, {"expectedVersion", 3}}));
            check(driver.task->phase() == AssistantTask::Phase::PreviewReady &&
                      driver.task->result()["providerTurns"] == 5 &&
                      encodeBodies(doc.bodies()) == original,
                  "Fourteen profiles extrude and array in five turns without topology rediscovery");
            driver.task->apply();
            const auto measured =
                measureEntity(doc, {doc.bodies().rbegin()->first, SelectionKind::Body, 0});
            check(driver.task->result()["applied"] == true && doc.bodies().size() == 26 &&
                      doc.history().total == history + 1 && measured.world.bounds &&
                      std::abs(measured.world.bounds->high.z - 1.75) < 1e-6,
                  "Native assistant array creates exactly eleven copies at the requested spacing");
            doc.undo();
            check(encodeBodies(doc.bodies()) == original,
                  "One undo restores the pre-assistant model");
        }
        {
            Fixture fixture;
            auto &doc = fixture.doc;
            const auto bytes = encodeContainer(doc);
            const auto originalBodies = encodeBodies(doc.bodies());
            const auto stamp = doc.saveStamp();
            const auto history = doc.history().total;
            NativeAssistantSession session(doc, files.path() + "/apply", &fixture.selection);
            Driver driver(session, doc);
            check(session.state().value("selectionAvailable") == true &&
                      driver.last.value("data").toObject().value("items").toArray().size() == 1,
                  "Actual native selection enters initial context");
            const auto diagnostic =
                driver
                    .send("geometry.diagnose",
                          query(doc, "geometry.diagnose",
                                {{"target", inspectionReference(doc, fixture.body)}}))["data"]
                    .toObject();
            check(diagnostic["solidStatus"] == "topology_blocked" &&
                      diagnostic["findings"].toArray()[0].toObject()["count"] == 4 &&
                      doc.isCurrentSnapshot(stamp) && encodeContainer(doc) == bytes,
                  "Assistant tools expose identical immutable open-boundary diagnostics");
            const auto sealed = driver.stage(fixture.body);
            const auto pinned = session.previewEdit(sealed);
            check(encodeContainer(doc) == bytes && doc.isCurrentSnapshot(stamp),
                  "Private native staging leaves the same live object unchanged");
            check(pinned->baseRevision() == doc.revision() &&
                      std::abs(
                          measureEntity(pinned->snapshot(), {fixture.body, SelectionKind::Body, 0})
                              .world.bounds->low.x -
                          .5) < 1e-6,
                  "Pinned native preview contains the exact immutable proposed geometry");
            driver.task->apply();
            check(
                driver.task->result().value("applied") == true && doc.owns(stamp) &&
                    doc.history().total == history + 1 &&
                    doc.history().entries.back().metadata.assistant &&
                    doc.history().entries.back().metadata.request.find("half a metre") !=
                        std::string::npos,
                "Durable native publication preserves existing history and adds one labeled undo");
            const auto committed = encodeContainer(doc);
            driver.task->apply();
            check(encodeContainer(doc) == committed && doc.history().total == history + 1,
                  "Repeated host Apply does not replay publication");
            doc.undo();
            check(encodeBodies(doc.bodies()) == originalBodies,
                  "Native undo uses existing document history");
            check(std::abs(measureEntity(doc, {fixture.body, SelectionKind::Body, 0})
                               .world.bounds->low.x) < 1e-6,
                  "Native undo restores original position");
            doc.redo();
            check(
                std::abs(
                    measureEntity(doc, {fixture.body, SelectionKind::Body, 0}).world.bounds->low.x -
                    .5) < 1e-6,
                "Native redo restores the committed edit");
            check(std::abs(measureEntity(pinned->snapshot(), {fixture.body, SelectionKind::Body, 0})
                               .world.bounds->low.x -
                           .5) < 1e-6,
                  "Pinned preview stays immutable after live undo and redo");
            session.close();
            rejects("SCOPE_CLOSED", [&] { session.state(); });
        }
        {
            Fixture fixture;
            NativeAssistantSession session(fixture.doc, files.path() + "/stale",
                                           &fixture.selection);
            Driver driver(session, fixture.doc);
            const auto sealed = driver.stage(fixture.body);
            const auto pinned = session.previewEdit(sealed);
            fixture.doc.move(fixture.body, {0, 2, 0});
            const auto human = encodeContainer(fixture.doc);
            driver.task->apply();
            check(driver.task->phase() == AssistantTask::Phase::Stale &&
                      encodeContainer(fixture.doc) == human &&
                      driver.task->result().value("applied") == false,
                  "Manual native edit wins over sealed preview");
            rejects("TRANSACTION_NOT_PENDING", [&] { session.previewEdit(sealed); });
            check(pinned->baseRevision() + 1 == fixture.doc.revision(),
                  "Previously pinned immutable data cannot bypass stale publication checks");
        }
        {
            Fixture fixture;
            NativeAssistantSession session(fixture.doc, files.path() + "/lock", &fixture.selection);
            Driver driver(session, fixture.doc);
            const auto sealed = driver.stage(fixture.body);
            const auto before = encodeContainer(fixture.doc);
            fixture.selection.lock(fixture.doc, fixture.body, true);
            auto commit = operation(fixture.doc, "transaction.commit",
                                    {{"requestId", sealed.value("requestId")},
                                     {"payloadHash", sealed.value("payloadHash")}});
            commit["documentId"] = "another-document";
            rejects("WRONG_DOCUMENT", [&] { session.execute(commit); });
            commit["documentId"] = QString::fromStdString(fixture.doc.identity());
            commit["extra"] = true;
            rejects("INVALID_REQUEST", [&] { session.execute(commit); });
            commit.remove("extra");
            commit["operation"] = "transaction.status";
            check(session.execute(commit).value("status") == "pending",
                  "Rejected malformed native commit does not retire a valid sealed proposal");
            rejects("LOCKED_ENTITY", [&] { session.previewEdit(sealed); });
            driver.task->apply();
            driver.task->reconcile();
            check(driver.task->result().value("applied") == false &&
                      encodeContainer(fixture.doc) == before,
                  "Temporary native locks added after sealing prevent publication");
        }
        {
            Fixture fixture;
            bool busy{};
            NativeAssistantSession session(fixture.doc, files.path() + "/busy", &fixture.selection,
                                           [&] { return busy; });
            busy = true;
            rejects("EDITOR_BUSY", [&] {
                session.execute(
                    operation(fixture.doc, "transaction.begin",
                              {{"expectedRevision", QString::number(fixture.doc.revision())}}));
            });
            check(session.state().value("busy") == true, "Native gesture state is explicit");
            busy = false;
            Driver driver(session, fixture.doc);
            driver.stage(fixture.body);
            const auto bytes = encodeContainer(fixture.doc);
            driver.task->cancel();
            check(encodeContainer(fixture.doc) == bytes &&
                      driver.task->phase() == AssistantTask::Phase::Canceled,
                  "Discard never mutates native model");
            rejects("UNSUPPORTED_CAPABILITY",
                    [&] { session.execute(operation(fixture.doc, "document.save")); });
            auto foreign = std::async(std::launch::async,
                                      [&] { rejects("WRONG_THREAD", [&] { session.state(); }); });
            foreign.get();
            fixture.doc = decodeContainer(bytes);
            rejects("SCOPE_CLOSED", [&] { session.state(); });
            rejects("SCOPE_CLOSED",
                    [&] { session.execute(query(fixture.doc, "document.describe")); });
        }
        {
            Fixture fixture;
            const auto before = encodeContainer(fixture.doc);
            const auto history = fixture.doc.history().total;
            bool armed{};
            TransactionCoordinator::Options options;
            options.fault = [&](auto phase) {
                if (armed && phase == OutcomeStore::Phase::AfterRename) {
                    armed = false;
                    throw std::runtime_error("Injected native publication uncertainty");
                }
            };
            NativeAssistantSession session(fixture.doc, files.path() + "/uncertain",
                                           &fixture.selection, {}, options);
            Driver driver(session, fixture.doc);
            driver.stage(fixture.body);
            armed = true;
            driver.task->apply();
            check(driver.task->phase() == AssistantTask::Phase::OutcomeUnknown &&
                      driver.task->result().value("applied").isNull() && session.uncertain() &&
                      encodeContainer(fixture.doc) == before,
                  "Unknown native durability is not reported as success or non-commit");
            rejects("OUTCOME_UNKNOWN", [&] { session.close(); });
            driver.task->reconcile();
            check(driver.task->result().value("applied") == true && !session.uncertain() &&
                      fixture.doc.history().total == history + 1,
                  "Native reconciliation publishes durable candidate once without replay");
            driver.task->apply();
            check(fixture.doc.history().total == history + 1,
                  "Apply retry after reconciliation preserves one undo");
        }
        {
            Fixture fixture;
            TransactionCoordinator actor(TransactionCoordinator::BorrowedDocument{fixture.doc},
                                         files.path() + "/borrowed");
            fixture.doc = decodeContainer(encodeContainer(fixture.doc));
            rejects("SCOPE_CLOSED", [&] { actor.document(); });
        }
        std::cout << "Native borrowed-document staging, preview, undo, selection, staleness, scope "
                     "and reconciliation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
