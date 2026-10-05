#include "core/assets.hpp"
#include "io/outcome_store.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <csignal>
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
    } catch (const OutcomeStoreError &error) {
        check(error.code() == code, error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QString hash(QByteArray value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              file.write(bytes) == bytes.size(),
          "Write fixture");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Temporary ledger directory");
        Document before;
        const auto body = before.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        before.markSaved();
        auto after = before;
        after.move(body, {2, 0, 0});
        const auto doc = QString::fromStdString(before.identity());
        const auto payload = hash("move two meters");
        const QJsonObject result{{"revision", QString::number(after.revision())},
                                 {"created", QJsonArray{}},
                                 {"taskId", "fixture-task"}};
        auto now = QDateTime::fromString("2026-10-05T00:00:00.000Z", Qt::ISODateWithMs);
        QString committedId, root = files.path() + "/main";
        {
            OutcomeStore store(root, doc, {}, [&] { return now; });
            rejects("STORE_BUSY", [&] { OutcomeStore second(root, doc); });
            const auto pending = store.begin(before, payload);
            committedId = pending["requestId"].toString();
            check(pending["status"] == "pending" && store.latestAfter().isEmpty(),
                  "Begin cannot publish a model");
            rejects("REQUEST_CONFLICT", [&] { store.lookup(committedId, hash("different")); });
            const auto committed = store.commit(committedId, payload, before, after, result);
            check(committed["status"] == "committed" && committed["result"].toObject() == result &&
                      store.latestBefore() == encodeContainer(before) &&
                      store.latestAfter() == encodeContainer(after),
                  "Commit must co-record exact before/after snapshots and original result");
            rejects("RECONCILIATION_REQUIRED", [&] { store.begin(before, payload); });
            auto divergent = before;
            divergent.move(body, {3, 0, 0});
            rejects("RECONCILIATION_REQUIRED", [&] { store.begin(divergent, payload); });
            const auto checkpoint = read(store.directory() + "/OUTCOMES");
            check(store.commit(committedId, payload, Document(), Document(), {}) == committed &&
                      store.abort(committedId, payload, "cancelled_after_commit") == committed &&
                      read(store.directory() + "/OUTCOMES") == checkpoint,
                  "Retry and late cancellation return the original commit without rewriting");
            rejects("REQUEST_CONFLICT",
                    [&] { store.commit(committedId, hash("other"), before, after, result); });
            const auto cancel = store.begin(after, hash("cancel"));
            const auto cancelId = cancel["requestId"].toString();
            const auto aborted = store.abort(cancelId, hash("cancel"), "cancelled_before_commit");
            check(aborted["status"] == "aborted" &&
                      store.commit(cancelId, hash("cancel"), before, after, result) == aborted,
                  "Aborted identity cannot later commit");
            const auto expires = store.begin(after, hash("expire"), 1);
            now = now.addSecs(1);
            check(store.commit(expires["requestId"].toString(), hash("expire"), before, after,
                               result)["status"] == "aborted",
                  "Expired pending request cannot commit");
            const auto wrongBase = store.begin(after, hash("base"));
            rejects("STALE_REVISION", [&] {
                store.commit(wrongBase["requestId"].toString(), hash("base"), before, after,
                             result);
            });
            check(store.lookup("99999", payload)["status"] == "unknown" &&
                      store.commit("99999", payload, before, after, result)["status"] == "unknown",
                  "Unknown request identity cannot create work");
        }
        {
            OutcomeStore reopened(root, doc, {}, [&] { return now; });
            check(reopened.lookup(committedId, payload)["status"] == "committed" &&
                      decodeContainer(reopened.latestAfter()).revision() == after.revision(),
                  "Restart must recover committed outcome and candidate together");
        }
        // Bound retention never evicts a young outcome, and pruning does not recycle IDs.
        {
            OutcomeStore bounded(files.path() + "/retention", doc, {2, 1024 * 1024},
                                 [&] { return now; });
            auto one = bounded.begin(before, payload);
            auto id1 = one["requestId"].toString();
            bounded.abort(id1, payload, "one");
            auto two = bounded.begin(before, payload);
            auto id2 = two["requestId"].toString();
            bounded.abort(id2, payload, "two");
            now = now.addDays(29);
            rejects("OUTCOME_LIMIT", [&] { bounded.begin(before, payload); });
            now = now.addDays(-50);
            rejects("OUTCOME_LIMIT", [&] { bounded.begin(before, payload); });
            now = now.addDays(52);
            const auto third = bounded.begin(before, payload);
            check(third["requestId"] == "3" &&
                      bounded.lookup(id1, payload)["status"] == "unknown" &&
                      bounded.commit(id1, payload, before, after, result)["status"] == "unknown",
                  "Expired outcomes remain unavailable and IDs are never recycled");
        }
        {
            OutcomeStore tiny(files.path() + "/tiny", doc, {4, 16 * 1024});
            rejects("OUTCOME_LIMIT", [&] { tiny.begin(before, payload); });
            check(tiny.lookup("1", payload)["status"] == "unknown",
                  "Failed admission cannot persist a pending identity");
        }
        {
            Document resource;
            auto asset = createAsset(
                resource, "payload", "application/octet-stream",
                std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(200 * 1024, 17)));
            auto grown = resource;
            replaceAsset(
                grown, asset,
                std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(210 * 1024, 18)));
            OutcomeStore bounded(files.path() + "/commit-budget",
                                 QString::fromStdString(resource.identity()), {4, 400 * 1024});
            const auto id = bounded.begin(resource, payload)["requestId"].toString();
            const auto original = read(bounded.directory() + "/OUTCOMES");
            rejects("OUTCOME_LIMIT", [&] { bounded.commit(id, payload, resource, grown, {}); });
            check(read(bounded.directory() + "/OUTCOMES") == original && !bounded.uncertain(),
                  "Over-budget co-recorded snapshots must fail before disk effects");
            check(bounded.abort(id, payload, "commit_exceeded_capacity")["status"] == "aborted",
                  "Rejected commit must retain capacity to record a terminal abort");
        }
        for (const auto phase :
             {OutcomeStore::Phase::BeforeWrite, OutcomeStore::Phase::AfterWrite,
              OutcomeStore::Phase::AfterFileSync, OutcomeStore::Phase::AfterRename,
              OutcomeStore::Phase::AfterDirectorySync}) {
            bool armed = false;
            OutcomeStore store(
                files.path() + "/fault-" + QString::number(int(phase)), doc, {},
                [&] { return now; },
                [&](auto value) {
                    if (armed && value == phase)
                        throw std::runtime_error("injected write failure");
                });
            const auto id = store.begin(before, payload)["requestId"].toString();
            armed = true;
            bool failed = false;
            try {
                store.commit(id, payload, before, after, result);
            } catch (const std::runtime_error &) {
                failed = true;
            }
            check(failed, "Storage fault must propagate");
            armed = false;
            const bool replaced = phase == OutcomeStore::Phase::AfterRename ||
                                  phase == OutcomeStore::Phase::AfterDirectorySync;
            check(store.uncertain() == replaced,
                  "Only possible replacement creates an uncertain outcome");
            if (replaced) {
                rejects("OUTCOME_UNKNOWN", [&] { store.lookup(id, payload); });
                rejects("OUTCOME_UNKNOWN", [&] { store.abort(id, payload, "cannot_guess"); });
                rejects("OUTCOME_UNKNOWN", [&] { store.begin(before, payload); });
                store.reconcile();
                check(store.lookup(id, payload)["status"] == "committed" &&
                          store.latestAfter() == encodeContainer(after),
                      "Reconciliation resolves complete replaced checkpoint to committed");
            } else {
                check(store.lookup(id, payload)["status"] == "pending" &&
                          store.latestAfter().isEmpty(),
                      "Pre-replacement failures must retain the exact pending baseline");
                check(store.abort(id, payload, "write_failed_before_commit")["status"] == "aborted",
                      "Verified absence can be aborted truthfully");
            }
        }
        // Validate every truncated prefix and a complete corrupted payload without overwriting
        // evidence.
        QString corruptRoot = files.path() + "/corrupt", corruptPath;
        QByteArray valid;
        {
            OutcomeStore store(corruptRoot, doc);
            const auto id = store.begin(before, payload)["requestId"].toString();
            store.commit(id, payload, before, after, result);
            corruptPath = store.directory() + "/OUTCOMES";
            valid = read(corruptPath);
        }
        for (qsizetype n = 0; n < valid.size(); ++n) {
            const auto truncated = valid.first(n);
            write(corruptPath, truncated);
            rejects("CORRUPT_OUTCOMES", [&] { OutcomeStore invalid(corruptRoot, doc); });
            check(read(corruptPath) == truncated,
                  "Corrupt evidence must not be rewritten during open");
        }
        auto corrupt = valid;
        corrupt[corrupt.size() - 1] ^= 1;
        write(corruptPath, corrupt);
        rejects("CORRUPT_OUTCOMES", [&] { OutcomeStore invalid(corruptRoot, doc); });
        // Correct checksums cannot substitute for semantic/native-document validation.
        const auto metaSize = qFromLittleEndian<quint32>(valid.constData() + 8);
        const auto originalMeta = QJsonDocument::fromJson(valid.mid(44, metaSize)).object();
        auto rewrite = [&](QJsonObject meta, QByteArray snapshots) {
            const auto json = QJsonDocument(meta).toJson(QJsonDocument::Compact);
            const auto payloadBytes = json + snapshots;
            QByteArray bytes = valid.first(12);
            qToLittleEndian<quint32>(json.size(), bytes.data() + 8);
            bytes += QCryptographicHash::hash(payloadBytes, QCryptographicHash::Sha256);
            bytes += payloadBytes;
            write(corruptPath, bytes);
        };
        auto invalidMeta = originalMeta;
        invalidMeta["nextRequestId"] = "0";
        rewrite(invalidMeta, valid.mid(44 + metaSize));
        rejects("CORRUPT_OUTCOMES", [&] { OutcomeStore invalid(corruptRoot, doc); });
        auto nativeBytes = valid.mid(44 + metaSize);
        const auto beforeBytes = originalMeta["beforeBytes"].toString().toLongLong();
        nativeBytes[beforeBytes] = 'X';
        invalidMeta = originalMeta;
        invalidMeta["afterHash"] = hash(nativeBytes.mid(beforeBytes));
        rewrite(invalidMeta, nativeBytes);
        rejects("CORRUPT_OUTCOMES", [&] { OutcomeStore invalid(corruptRoot, doc); });
        write(corruptPath, valid);
        {
            OutcomeStore restored(corruptRoot, doc);
            check(restored.latestAfter() == encodeContainer(after),
                  "Valid checkpoint reopens after restoring evidence");
        }
        QFile::remove(corruptPath);
        rejects("CORRUPT_OUTCOMES", [&] { OutcomeStore missing(corruptRoot, doc); });
        // Real killed writers exercise stale-lock release and complete old/new atomic replacement.
        for (const auto phase :
             {OutcomeStore::Phase::AfterFileSync, OutcomeStore::Phase::AfterRename}) {
            const auto killedRoot = files.path() + "/kill-" + QString::number(int(phase));
            QString id;
            {
                OutcomeStore store(killedRoot, doc);
                id = store.begin(before, payload)["requestId"].toString();
            }
            const auto child = ::fork();
            check(child >= 0, "Fork killed writer");
            if (child == 0) {
                try {
                    OutcomeStore store(killedRoot, doc, {}, QDateTime::currentDateTimeUtc,
                                       [&](auto value) {
                                           if (value == phase)
                                               ::kill(::getpid(), SIGKILL);
                                       });
                    store.commit(id, payload, before, after, result);
                } catch (...) {
                    ::_exit(2);
                }
                ::_exit(3);
            }
            int status = 0;
            check(::waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
                      WTERMSIG(status) == SIGKILL,
                  "Writer must be killed at requested durability boundary");
            OutcomeStore recovered(killedRoot, doc);
            const auto expected =
                phase == OutcomeStore::Phase::AfterRename ? "committed" : "pending";
            check(recovered.lookup(id, payload)["status"] == expected,
                  "Killed writer must expose a complete old or new checkpoint");
            if (phase == OutcomeStore::Phase::AfterRename)
                check(recovered.latestAfter() == encodeContainer(after),
                      "Killed committed writer retains exact candidate");
            else
                check(recovered.abort(id, payload, "restart_before_commit")["status"] == "aborted",
                      "No complete commit may resolve to aborted");
        }
        std::cout << "Durable outcomes, retries, retention, storage faults, truncation and killed "
                     "writers passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
