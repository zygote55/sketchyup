#include "automation/commands.hpp"
#include "automation/transaction_coordinator.hpp"
#include "core/components.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <csignal>
#include <future>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
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
    } catch (const OutcomeStoreError &e) {
        check(e.code() == code, e.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QJsonObject batch(const Document &doc, QJsonArray commands = {}) {
    if (commands.empty())
        commands = QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                          {"body", "7"},
                                          {"delta", QJsonArray{.5, 0, 0}}},
                              QJsonObject{{"command", "document.units"}, {"units", "m"}}};
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands},
            {"history", QJsonObject{{"label", "Adjust selected window"},
                                    {"taskId", "original-task"},
                                    {"request", "Move Window A and display meters"},
                                    {"assistant", true}}}};
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
        QTemporaryDir files;
        check(files.isValid(), "Transaction test directory");
        auto room = loadDocument(QStringLiteral(SOURCE_DIR "/examples/m4-room-study.sketchyup"));
        const auto original = encodeContainer(room);
        const auto baseRevision = room.revision();
        QString id, hash;
        QJsonObject committed;
        QByteArray committedBytes;
        const auto root = files.path() + "/basic";
        {
            TransactionCoordinator actor(room, root);
            const auto prepared = actor.prepare(batch(actor.document()));
            id = prepared["requestId"].toString();
            hash = prepared["payloadHash"].toString();
            check(actor.status(id, hash)["status"] == "pending" &&
                      encodeContainer(actor.document()) == original,
                  "Durable prepare must leave live model unchanged");
            const auto preview = actor.preview(id, hash);
            auto measurement =
                query(room, "measure.entity",
                      {{"target", inspectionReference(room, 7)}, {"space", "local"}});
            measurement["expectedRevision"] = preview["proposedRevision"];
            check(actor.inspect(id, hash, measurement)["status"] == "staged" &&
                      actor.diff(id, hash)["changes"].toArray().size() == 2,
                  "Prepared model must support bounded measurement and diff");
            committed = actor.commit(id, hash);
            committedBytes = encodeContainer(actor.document());
            check(committed["status"] == "committed" &&
                      actor.document().revision() == baseRevision + 1 &&
                      actor.document().history().total == 1 &&
                      actor.document().history().entries[0].metadata.taskId == "original-task" &&
                      actor.document().displayUnits() == DisplayUnit::Meters && !actor.uncertain(),
                  "Confirmed commit must publish once with original undo metadata");
            check(actor.commit(id, hash) == committed && actor.cancel(id, hash) == committed &&
                      actor.document().revision() == baseRevision + 1 &&
                      actor.document().history().total == 1,
                  "Lost response retry and late cancel cannot duplicate or undo the commit");
            rejects("REQUEST_CONFLICT", [&] { actor.commit(id, QString(64, 'a')); });
            const auto undo = committed["result"].toObject()["undo"].toObject();
            check(undo["taskId"] == "original-task" &&
                      undo["commitRevision"] == QString::number(baseRevision + 1) &&
                      !committed["result"].toObject()["mappings"].toObject().isEmpty(),
                  "Receipt must retain original undo identity and change mappings");
            actor.edit([](Document &doc) { doc.undo(); });
            check(!actor.document().dirty() &&
                      actor.document().displayUnits() == DisplayUnit::Millimeters &&
                      actor.status(id, hash) == committed,
                  "User undo is a new revision, not erasure of the original outcome");
            actor.edit([](Document &doc) { doc.redo(); });
            rejects("REENTRANT_TRANSACTION",
                    [&] { actor.edit([&](Document &) { actor.cancel(id, hash); }); });
            const auto revision = actor.document().revision();
            auto thread = std::async(std::launch::async, [&] {
                rejects("WRONG_THREAD", [&] { actor.edit([](Document &d) { d.undo(); }); });
            });
            thread.get();
            check(actor.document().revision() == revision,
                  "Wrong-thread/reentrant calls cannot mutate the model");
        }
        rejects("RECONCILIATION_REQUIRED", [&] { TransactionCoordinator old(room, root); });
        {
            TransactionCoordinator::Options options;
            options.mode = TransactionCoordinator::OpenMode::RecoverLatest;
            TransactionCoordinator recovered(room, root, options);
            check(encodeContainer(recovered.document()) == committedBytes &&
                      recovered.document().dirty() && recovered.document().history().total == 1 &&
                      recovered.document().history().entries[0].metadata.taskId ==
                          "original-task" &&
                      recovered.commit(id, hash) == committed,
                  "Explicit recovery must restore candidate, outcome and original undo entry "
                  "without replay");
            recovered.edit([](Document &doc) { doc.undo(); });
            check(recovered.document().dirty() &&
                      recovered.document().displayUnits() == DisplayUnit::Millimeters,
                  "Recovered undo baseline must not pretend to be an explicit save");
        }
        {
            TransactionCoordinator actor(room, files.path() + "/stale");
            auto p = actor.prepare(batch(actor.document()));
            const auto r = p["requestId"].toString(), h = p["payloadHash"].toString();
            actor.edit([](Document &doc) { doc.move(8, {0, .25, 0}); });
            const auto changed = encodeContainer(actor.document());
            const auto stale = actor.commit(r, h);
            check(stale["status"] == "aborted" &&
                      stale["result"].toObject()["reason"] == "STALE_REVISION" &&
                      encodeContainer(actor.document()) == changed,
                  "Intervening manual edit must reject the stale commit without overwriting it");
            p = actor.prepare(batch(actor.document()));
            actor.edit([](Document &doc) {
                doc.move(8, {0, .25, 0});
                doc.undo();
            });
            check(actor.commit(p["requestId"].toString(), p["payloadHash"].toString())["status"] ==
                      "aborted",
                  "Edit followed by undo still invalidates a prepared request");
            p = actor.prepare(batch(actor.document()));
            const auto cancelled =
                actor.cancel(p["requestId"].toString(), p["payloadHash"].toString());
            check(cancelled["status"] == "aborted" &&
                      actor.commit(p["requestId"].toString(), p["payloadHash"].toString()) ==
                          cancelled,
                  "Cancelled task cannot later publish");
            check(actor.commit("999999", QString(64, 'b'))["status"] == "unknown",
                  "Unknown identity must never create work");
        }
        {
            StagingSession::Clock::time_point monotonic{};
            auto wall = QDateTime::currentDateTimeUtc();
            TransactionCoordinator::Options options;
            options.monotonic = [&] { return monotonic; };
            options.wallClock = [&] { return wall; };
            TransactionCoordinator actor(room, files.path() + "/expiry", options);
            const auto p = actor.prepare(batch(actor.document()), 1);
            monotonic += std::chrono::seconds(1);
            check(actor.commit(p["requestId"].toString(), p["payloadHash"].toString())["status"] ==
                          "aborted" &&
                      encodeContainer(actor.document()) == original,
                  "Monotonic expiry cannot be extended by a stationary wall clock");
        }
        {
            auto shared = room;
            const auto definition = shared.instances().at(8)->definition;
            placeComponent(shared, definition, Transform::translation({0, 2, 0}));
            shared.markSaved();
            Id member = 0;
            for (const auto &[id, body] : shared.definitions().at(definition)->members)
                if (!body->surface.vertices.empty()) {
                    member = id;
                    break;
                }
            check(member != 0, "Shared component fixture needs geometry");
            const auto commands = QJsonArray{QJsonObject{
                {"command", "component.edit"},
                {"definition", QString::number(definition)},
                {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                    {"body", QString::number(member)},
                                                    {"delta", QJsonArray{0, 0, .05}}}}}}};
            const auto sharedRoot = files.path() + "/shared";
            QByteArray edited;
            {
                TransactionCoordinator actor(shared, sharedRoot);
                const auto p = actor.prepare(batch(actor.document(), commands));
                check(actor.commit(p["requestId"].toString(),
                                   p["payloadHash"].toString())["status"] == "committed",
                      "Shared definition proposal must commit");
                edited = encodeContainer(actor.document());
            }
            TransactionCoordinator::Options options;
            options.mode = TransactionCoordinator::OpenMode::RecoverLatest;
            TransactionCoordinator recovered(shared, sharedRoot, options);
            check(encodeContainer(recovered.document()) == edited &&
                      recovered.document().history().total == 1,
                  "Recovery must restore changed shared definitions/bindings and preserve owned "
                  "assets exactly");
            recovered.edit([](Document &doc) { doc.undo(); });
            check(*recovered.document().definitions().at(definition)->members.at(member) ==
                      *shared.definitions().at(definition)->members.at(member),
                  "Recovered shared definition must undo to its original geometry");
        }
        {
            Document empty;
            TransactionCoordinator actor(empty, files.path() + "/mapping-budget");
            QJsonArray face;
            for (const auto point : std::vector<Vec3>{{5, 0, 0},
                                                      {5, 2, 0},
                                                      {4, 4, 0},
                                                      {2, 5, 0},
                                                      {0, 5, 0},
                                                      {-2, 5, 0},
                                                      {-4, 4, 0},
                                                      {-5, 2, 0},
                                                      {-5, 0, 0},
                                                      {-5, -2, 0},
                                                      {-4, -4, 0},
                                                      {-2, -5, 0},
                                                      {0, -5, 0},
                                                      {2, -5, 0},
                                                      {4, -4, 0},
                                                      {5, -2, 0}})
                face.append(QJsonArray{point.x, point.y, point.z});
            QJsonArray commands;
            for (int i = 0; i < 100; ++i)
                commands.append(
                    QJsonObject{{"command", "geometry.face"}, {"loops", QJsonArray{face}}});
            const auto p = actor.prepare(batch(actor.document(), commands));
            const auto r = p["requestId"].toString(), h = p["payloadHash"].toString();
            rejects("LIMIT_EXCEEDED", [&] { actor.commit(r, h); });
            check(actor.document().bodies().empty() && !actor.document().canUndo() &&
                      actor.cancel(r, h)["status"] == "aborted",
                  "Oversized mapping receipt must fail before publication and remain cancellable");
        }
        QString orphanId, orphanHash;
        const auto orphanRoot = files.path() + "/orphan";
        {
            TransactionCoordinator actor(room, orphanRoot);
            const auto p = actor.prepare(batch(actor.document()));
            orphanId = p["requestId"].toString();
            orphanHash = p["payloadHash"].toString();
        }
        {
            TransactionCoordinator reopened(room, orphanRoot);
            check(reopened.status(orphanId, orphanHash)["status"] == "aborted" &&
                      encodeContainer(reopened.document()) == original,
                  "Restart without private staging must durably abort its uncommitted identity");
        }
        for (const auto phase :
             {OutcomeStore::Phase::AfterFileSync, OutcomeStore::Phase::AfterRename,
              OutcomeStore::Phase::AfterDirectorySync}) {
            bool armed = false;
            TransactionCoordinator::Options options;
            options.fault = [&](auto value) {
                if (armed && value == phase)
                    throw std::runtime_error("injected disk failure");
            };
            TransactionCoordinator actor(
                room, files.path() + "/fault-" + QString::number(int(phase)), options);
            const auto p = actor.prepare(batch(actor.document()));
            const auto r = p["requestId"].toString(), h = p["payloadHash"].toString();
            armed = true;
            bool failed = false;
            try {
                actor.commit(r, h);
            } catch (const std::runtime_error &) {
                failed = true;
            }
            armed = false;
            check(failed && encodeContainer(actor.document()) == original,
                  "Unconfirmed commit cannot publish a live edit");
            if (phase == OutcomeStore::Phase::AfterFileSync) {
                check(!actor.uncertain() && actor.status(r, h)["status"] == "pending",
                      "Pre-rename failure preserves retryable pending state");
                check(actor.commit(r, h)["status"] == "committed",
                      "Safe retry after confirmed nonreplacement commits once");
            } else {
                check(actor.uncertain() && actor.status(r, h)["status"] == "unknown",
                      "Possible replacement must report unknown");
                rejects("OUTCOME_UNKNOWN",
                        [&] { actor.edit([](Document &doc) { doc.move(8, {1, 0, 0}); }); });
                rejects("OUTCOME_UNKNOWN", [&] { actor.cancel(r, h); });
                const auto resolved = actor.reconcile();
                check(resolved["status"] == "committed" &&
                          actor.document().revision() == baseRevision + 1 &&
                          actor.document().history().total == 1 && !actor.uncertain(),
                      "Reconciliation must publish the verified candidate exactly once");
                check(actor.commit(r, h) == resolved && actor.document().history().total == 1,
                      "Reconciled retry returns original receipt");
            }
        }
        {
            bool armed = false;
            TransactionCoordinator::Options options;
            options.fault = [&](auto phase) {
                if (armed && phase == OutcomeStore::Phase::AfterRename)
                    throw std::runtime_error("lost begin response");
            };
            TransactionCoordinator actor(room, files.path() + "/uncertain-begin", options);
            armed = true;
            rejects("OUTCOME_UNKNOWN", [&] { actor.prepare(batch(actor.document())); });
            armed = false;
            check(actor.uncertain() && encodeContainer(actor.document()) == original,
                  "Uncertain identity issuance cannot mutate the model");
            actor.reconcile();
            const auto p = actor.prepare(batch(actor.document()));
            check(p["requestId"] == "2" && !actor.uncertain(),
                  "Reconciliation must retire orphaned staging without recycling accepted "
                  "identities");
        }
        {
            const auto conflictRoot = files.path() + "/rename-failure";
            const auto path =
                QDir(conflictRoot)
                    .filePath(QString::fromStdString(room.identity()) + "-1/OUTCOMES");
            bool armed = false;
            TransactionCoordinator::Options options;
            options.fault = [&](auto phase) {
                if (armed && phase == OutcomeStore::Phase::AfterFileSync) {
                    check(QFile::rename(path, path + ".old") && QDir().mkdir(path),
                          "Inject rename target conflict");
                    QFile blocker(path + "/blocker");
                    check(blocker.open(QIODevice::WriteOnly),
                          "Make conflicting directory nonempty");
                    blocker.write("conflict");
                }
            };
            TransactionCoordinator actor(room, conflictRoot, options);
            const auto p = actor.prepare(batch(actor.document()));
            const auto r = p["requestId"].toString(), h = p["payloadHash"].toString();
            armed = true;
            rejects("OUTCOME_UNKNOWN", [&] { actor.commit(r, h); });
            armed = false;
            check(QDir(path).removeRecursively() && QFile::rename(path + ".old", path),
                  "Restore original pending checkpoint for reconciliation");
            const auto outcome = actor.reconcile();
            check(outcome["status"] == "aborted" &&
                      outcome["result"].toObject()["reason"] == "VERIFIED_NO_COMMIT" &&
                      encodeContainer(actor.document()) == original && !actor.uncertain(),
                  "Verified failed replacement must abort without publishing a candidate");
        }
        {
            const auto killedRoot = files.path() + "/killed-coordinator";
            const auto child = ::fork();
            check(child >= 0, "Fork transaction actor");
            if (child == 0) {
                bool armed = false;
                try {
                    TransactionCoordinator::Options options;
                    options.fault = [&](auto phase) {
                        if (armed && phase == OutcomeStore::Phase::AfterDirectorySync)
                            ::kill(::getpid(), SIGKILL);
                    };
                    TransactionCoordinator actor(room, killedRoot, options);
                    const auto p = actor.prepare(batch(actor.document()));
                    armed = true;
                    actor.commit(p["requestId"].toString(), p["payloadHash"].toString());
                } catch (...) {
                    ::_exit(2);
                }
                ::_exit(3);
            }
            int status = 0;
            check(::waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
                      WTERMSIG(status) == SIGKILL,
                  "Actor must die after durable commit before live publication");
            TransactionCoordinator::Options options;
            options.mode = TransactionCoordinator::OpenMode::RecoverLatest;
            TransactionCoordinator recovered(room, killedRoot, options);
            check(encodeContainer(recovered.document()) == committedBytes &&
                      recovered.document().history().total == 1 &&
                      recovered.document().history().entries[0].metadata.taskId == "original-task",
                  "Restart after prepublication kill restores exactly one original task and its "
                  "undo");
        }
        std::cout << "Durable publication, original undo, stale guards, retry, expiry and "
                     "reconciliation passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
