#include "core/assets.hpp"
#include "core/components.hpp"
#include "io/native_limits.hpp"
#include "io/outcome_store.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
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
template <class F> void refuses(const char *code, const char *message, F operation) {
    try {
        operation();
    } catch (const OutcomeStoreError &error) {
        check(error.code() == code && std::string(error.what()) == message, error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
// D-089: one 160-sided face placed `placements` times. Each placement materializes its own copy of
// the face in memory, so the in-memory size grows with the placements, but the container stores the
// definition once and one small record per placement. The marker is a loose body to edit.
Document placedModel(int placements, Id &marker) {
    Document doc;
    marker = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, "Marker");
    std::vector<Vec3> ring;
    for (int i = 0; i < 160; ++i) {
        const double angle = 2 * 3.14159265358979323846 * i / 160;
        ring.push_back({std::cos(angle), std::sin(angle), 0});
    }
    const auto component = createComponent(doc, doc.addFace({ring}, "Ring"), "Ring");
    for (int i = 1; i < placements; ++i)
        placeComponent(doc, component.definition, Transform::translation({0, 2.0 * i, 0}));
    return doc;
}
// `count` payloads of `bytes` each, with a loose body to edit.
Document payloadModel(int count, size_t bytes, Id &marker) {
    Document doc;
    marker = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, "Marker");
    for (int i = 0; i < count; ++i)
        createAsset(doc, "Payload " + std::to_string(i), "application/octet-stream",
                    std::make_shared<const AssetPayload>(
                        std::vector<std::uint8_t>(bytes, std::uint8_t(i + 1))));
    return doc;
}
// The process's peak resident set (VmHWM) in MiB, or -1 where /proc is unavailable.
double peakMiB() {
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    for (const auto &line : status.readAll().split('\n'))
        if (line.startsWith("VmHWM:"))
            return line.mid(6).trimmed().split(' ').first().toDouble() / 1024;
    return -1;
}
// `--measure-commit assets|placed`: one durable commit of a large synthetic model, timed, with the
// process's peak resident set before and after it. Run alone so the peak is the commit's own.
int measureCommit(const QString &model) {
    QTemporaryDir files;
    Id marker{};
    auto before =
        model == "placed" ? placedModel(3000, marker) : payloadModel(4, 16 * 1024 * 1024, marker);
    auto after = before;
    after.move(marker, {1, 0, 0});
    const auto payload = hash("measure one commit");
    const QJsonObject result{{"revision", QString::number(after.revision())},
                             {"created", QJsonArray{}}};
    OutcomeStore store(files.path() + "/store", QString::fromStdString(before.identity()));
    const auto peakBefore = peakMiB();
    QElapsedTimer timer;
    timer.start();
    const auto id = store.begin(before, payload)["requestId"].toString();
    const auto beginMs = timer.restart();
    check(store.commit(id, payload, before, after, result)["status"] == "committed",
          "Measured commit must commit");
    const auto commitMs = timer.elapsed();
    std::cout << model.toStdString() << ": in-memory " << before.readSnapshotBytes()
              << " bytes, stored before " << store.latestBefore().size() << " and after "
              << store.latestAfter().size() << " bytes; begin " << beginMs << " ms, commit "
              << commitMs << " ms; peak RSS " << peakBefore << " MiB before the commit, "
              << peakMiB() << " MiB after it\n";
    return 0;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().value(1) == "--measure-commit")
            return measureCommit(app.arguments().value(2));
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
            // Begin admits the base, whose two copies and one result reservation fit the budget.
            // The after copy is larger than the base, so the commit's own check refuses the pair.
            Document resource;
            auto asset = createAsset(
                resource, "payload", "application/octet-stream",
                std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(100 * 1024, 17)));
            auto grown = resource;
            replaceAsset(
                grown, asset,
                std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(450 * 1024, 18)));
            OutcomeStore bounded(files.path() + "/commit-budget",
                                 QString::fromStdString(resource.identity()), {4, 512 * 1024});
            const auto id = bounded.begin(resource, payload)["requestId"].toString();
            const auto original = read(bounded.directory() + "/OUTCOMES");
            rejects("OUTCOME_LIMIT", [&] { bounded.commit(id, payload, resource, grown, {}); });
            check(read(bounded.directory() + "/OUTCOMES") == original && !bounded.uncertain(),
                  "Over-budget co-recorded snapshots must fail before disk effects");
            check(bounded.abort(id, payload, "commit_exceeded_capacity")["status"] == "aborted",
                  "Rejected commit must retain capacity to record a terminal abort");
        }
        {
            // D-089: the store charges what it writes. The model's in-memory size is over 64 MiB,
            // because each of 3,000 placements materializes the face, but its container stores the
            // definition once, so it is well under 32 MiB. Begin and commit must both accept it.
            Id marker{};
            auto placed = placedModel(3000, marker);
            check(placed.readSnapshotBytes() > 64 * 1024 * 1024 &&
                      encodeContainer(placed).size() < 32 * 1024 * 1024,
                  "Fixture must be over 64 MiB in memory and under 32 MiB stored");
            auto movedPlaced = placed;
            movedPlaced.move(marker, {1, 0, 0});
            OutcomeStore placedStore(files.path() + "/placed",
                                     QString::fromStdString(placed.identity()), {},
                                     [&] { return now; });
            const auto placedId = placedStore.begin(placed, payload)["requestId"].toString();
            check(placedStore.commit(placedId, payload, placed, movedPlaced, result)["status"] ==
                          "committed" &&
                      placedStore.latestAfter() == encodeContainer(movedPlaced),
                  "A model over 64 MiB in memory but small on disk must commit");
        }
        {
            // D-086: the budget holds a before and an after container of the largest valid file.
            // A model with the largest asset set (64 MiB of payloads) commits; on its own, each
            // container is over the old 64 MiB budget.
            check(OutcomeStore::Limits{}.bytes >= 2 * NativeLimits::containerBytes,
                  "The outcome budget must hold two copies of the largest valid container");
            Id marker{};
            auto largest = payloadModel(4, 16 * 1024 * 1024, marker);
            auto grown = largest;
            grown.move(marker, {1, 0, 0});
            OutcomeStore store(files.path() + "/largest",
                               QString::fromStdString(largest.identity()));
            const auto id = store.begin(largest, payload)["requestId"].toString();
            check(store.commit(id, payload, largest, grown, result)["status"] == "committed" &&
                      store.latestBefore().size() > 64 * 1024 * 1024 &&
                      store.latestAfter() == encodeContainer(grown),
                  "A model with 64 MiB of payloads must commit its exact before and after");
        }
        {
            // D-086: 42 MiB of payloads is over the old budget once its two copies are counted. It
            // commits under the new budget. The old budget refuses its begin with the same message
            // as before, and persists nothing.
            Id marker{};
            auto midsize = payloadModel(3, 14 * 1024 * 1024, marker);
            auto grown = midsize;
            grown.move(marker, {1, 0, 0});
            const auto midsizeDocument = QString::fromStdString(midsize.identity());
            OutcomeStore store(files.path() + "/midsize", midsizeDocument);
            const auto id = store.begin(midsize, payload)["requestId"].toString();
            check(store.commit(id, payload, midsize, grown, result)["status"] == "committed",
                  "A 42 MiB model must commit under the outcome budget");
            OutcomeStore oldBudget(files.path() + "/midsize-64", midsizeDocument,
                                   {4, 64 * 1024 * 1024}, [&] { return now; });
            refuses("OUTCOME_LIMIT", "Base document exceeds durable storage capacity",
                    [&] { oldBudget.begin(midsize, payload); });
            check(oldBudget.lookup("1", payload)["status"] == "unknown",
                  "A refused begin cannot persist a pending identity");
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
