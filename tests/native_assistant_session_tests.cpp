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
    Driver(NativeAssistantSession &session, Document &doc) : session(session), doc(doc) {
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
        options.allowedCommands = {"geometry.translate"};
        options.context = {query(doc, "selection.get")};
        task = std::make_unique<AssistantTask>(backend, options);
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
