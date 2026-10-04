#include "io/recovery.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace sketchy;
namespace {
int syncs{}, failSync{}, killSync{};
bool fullDisk{}, partialWrite{};
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected rejection");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read test file");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              file.write(bytes) == bytes.size(),
          "Write test file");
}
QByteArray frame(const QJsonObject &object) {
    const auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray header(4, '\0');
    qToLittleEndian<quint32>(data.size(), header.data());
    return header + QCryptographicHash::hash(data, QCryptographicHash::Sha256) + data;
}
QJsonObject payload(const QByteArray &bytes) {
    return QJsonDocument::fromJson(bytes.mid(36)).object();
}
QString journal(const QString &root, const QString &key) {
    const auto path = QDir(root).filePath(key);
    return QDir(path).filePath(payload(read(QDir(path).filePath("CURRENT")))["journal"].toString());
}
Document model() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}}, "Recovery box");
    doc.extrude(1, doc.bodies().at(1)->surface.faces.begin()->first, 2);
    return doc;
}
} // namespace
// Syscall fault injection is local to this executable, including killed child processes.
extern "C" int fsync(int fd) {
    ++syncs;
    if (syncs == killSync)
        raise(SIGKILL);
    if (syncs == failSync) {
        errno = ENOSPC;
        return -1;
    }
    return syscall(SYS_fsync, fd);
}
extern "C" ssize_t write(int fd, const void *bytes, size_t count) {
    struct stat st{};
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        if (fullDisk) {
            errno = ENOSPC;
            return -1;
        }
        if (partialWrite) {
            partialWrite = false;
            fullDisk = true;
            return syscall(SYS_write, fd, bytes, std::min<size_t>(13, count));
        }
    }
    return syscall(SYS_write, fd, bytes, count);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        const auto root = temporary.filePath("recovery");
        auto doc = model();
        const auto source = temporary.filePath("explicit.sketchyup");
        saveDocument(doc, source);
        const auto original = read(source);
        RecoveryContext context{source, doc.revision(), QDateTime::currentDateTimeUtc()};
        doc.move(1, {1, 0, 0});
        const auto first = captureRecovery(doc, context);
        const auto firstBytes = encodeContainer(doc);
        doc.paint(1, {.2, .3, .4});
        const auto second = captureRecovery(doc, context);
        const auto secondBytes = encodeContainer(doc);
        QString key;
        {
            RecoveryWriter writer(root, QString::fromStdString(doc.identity()));
            key = writer.key();
            auto acknowledged = writer.write(first);
            check(acknowledged.revision == first.info().revision && doc.dirty(),
                  "Snapshot completion reports captured revision without marking current state "
                  "saved");
            check(readRecovery(root, key).busy && listRecoveries(root).empty(),
                  "Active session is protected from readers and discard");
            rejects([&] { discardRecovery(root, key); });
            writer.write(second);
            rejects([&] { writer.write(first); });
        }
        auto recovered = readRecovery(root, key);
        check(recovered.verified && recovered.issue.isEmpty() && !recovered.incompleteTail &&
                  recovered.document && encodeContainer(*recovered.document) == secondBytes,
              "Contiguous journal recovers exact immutable snapshot");
        check(recovered.document->dirty() && !recovered.document->canUndo() &&
                  recovered.info.savedRevision == context.savedRevision &&
                  recovered.info.sourcePath == source,
              "Recovered document is dirty with preserved identity and explicit-save metadata, "
              "without invented undo history");
        check(read(source) == original, "Recovery never changes explicit save");
        recovered.document->move(1, {.1, 0, 0});
        recovered.document->undo();
        check(recovered.document->dirty(),
              "Undo back to recovered checkpoint still requires explicit save");
        saveDocument(*recovered.document, temporary.filePath("recovered-copy.sketchyup"));
        check(!recovered.document->dirty() && read(source) == original,
              "Explicit Save clears recovered dirty state without changing the source");
        {
            RecoveryWriter other(root, QString::fromStdString(doc.identity()));
            other.write(second);
            check(
                other.key() != key && readRecovery(root, key).verified,
                "Separate active session cannot overwrite earlier recovery for the same identity");
            other.discard();
        }
        const auto listed = listRecoveries(root);
        check(listed.size() == 1 && listed[0].verified && !listed[0].document,
              "Recovery listing reports verified metadata without retaining every model");
        const auto journalPath = journal(root, key);
        const auto complete = read(journalPath);
        for (qsizetype boundary = 0; boundary < complete.size(); ++boundary) {
            write(journalPath, complete.first(boundary));
            auto truncated = readRecovery(root, key);
            check(truncated.verified && truncated.issue.isEmpty() && truncated.document &&
                      encodeContainer(*truncated.document) == firstBytes &&
                      truncated.incompleteTail == (boundary > 0),
                  "Every interrupted frame boundary preserves checkpoint and exact recovered "
                  "revision");
        }
        write(journalPath, complete);
        auto damaged = complete;
        damaged[damaged.size() - 2] ^= 1;
        write(journalPath, damaged);
        auto corrupt = readRecovery(root, key);
        check(corrupt.verified && !corrupt.issue.isEmpty() && corrupt.document &&
                  encodeContainer(*corrupt.document) == firstBytes && read(journalPath) == damaged,
              "Interior corruption stops at verified prefix and preserves evidence");
        auto wrongChain = payload(complete);
        wrongChain["previous"] = QString(64, '0');
        write(journalPath, frame(wrongChain));
        check(!readRecovery(root, key).issue.isEmpty(),
              "Recomputed checksum cannot hide broken journal chain");
        auto traversal = payload(complete);
        traversal["snapshot"] = "../explicit.sketchyup";
        write(journalPath, frame(traversal));
        check(!readRecovery(root, key).issue.isEmpty() && read(source) == original,
              "Untrusted checkpoint path rejects without following it");
        write(journalPath, complete);
        const auto currentPath = QDir(root).filePath(key + "/CURRENT");
        const auto pointer = read(currentPath);
        auto badPointer = pointer;
        badPointer[10] ^= 1;
        write(currentPath, badPointer);
        check(!readRecovery(root, key).verified,
              "Corrupt checkpoint pointer makes no recovery claim");
        write(currentPath, pointer);
        const auto newestPath =
            QDir(root).filePath(key + "/" + payload(complete)["snapshot"].toString());
        const auto newest = read(newestPath);
        write(newestPath, newest.first(newest.size() - 1));
        check(readRecovery(root, key).info.revision == first.info().revision &&
                  !readRecovery(root, key).issue.isEmpty(),
              "Truncated snapshot stops at older verified prefix");
        write(newestPath, newest);
        check(QFile::remove(newestPath) && QFile::link(source, newestPath),
              "Create linked snapshot fixture");
        check(!readRecovery(root, key).issue.isEmpty(), "Linked snapshot is not followed");
        QFile::remove(newestPath);
        write(newestPath, newest);
        // Fault every sync boundary of checkpoint publication/compaction, and append.
        for (const bool compact : {false, true}) {
            for (int boundary = 1; boundary <= (compact ? 6 : 3); ++boundary) {
                const auto faultRoot =
                    temporary.filePath(QString("fault-%1-%2").arg(compact).arg(boundary));
                auto work = model();
                QString faultKey;
                quint64 baseline{}, final{};
                {
                    RecoveryWriter writer(faultRoot, QString::fromStdString(work.identity()));
                    faultKey = writer.key();
                    writer.write(captureRecovery(work));
                    if (compact)
                        for (int i = 0; i < 3; ++i) {
                            work.move(1, {.1, 0, 0});
                            writer.write(captureRecovery(work));
                        }
                    baseline = work.revision();
                    work.move(1, {.2, 0, 0});
                    final = work.revision();
                    syncs = 0;
                    failSync = boundary;
                    rejects([&] { writer.write(captureRecovery(work)); });
                    failSync = 0;
                }
                auto result = readRecovery(faultRoot, faultKey);
                check(result.verified && result.document &&
                          (result.info.revision == baseline || result.info.revision == final),
                      "Failed sync retains a complete valid generation without false "
                      "acknowledgement");
            }
        }
        for (const bool partial : {false, true}) {
            const auto faultRoot = temporary.filePath(partial ? "partial" : "full-disk");
            QString faultKey;
            {
                RecoveryWriter writer(faultRoot, QString::fromStdString(doc.identity()));
                faultKey = writer.key();
                writer.write(first);
                fullDisk = !partial;
                partialWrite = partial;
                rejects([&] { writer.write(second); });
                fullDisk = partialWrite = false;
            }
            auto result = readRecovery(faultRoot, faultKey);
            check(result.verified && result.info.revision == first.info().revision,
                  "Disk-full/partial-write preserves last durable snapshot");
        }
        // A retry creates a fresh generation; it never appends behind an uncertain tail.
        const auto retryRoot = temporary.filePath("retry");
        QString retryKey;
        {
            RecoveryWriter writer(retryRoot, QString::fromStdString(doc.identity()));
            retryKey = writer.key();
            writer.write(first);
            syncs = 0;
            failSync = 3;
            rejects([&] { writer.write(second); });
            failSync = 0;
            writer.write(second);
            check(QDir(QDir(retryRoot).filePath(retryKey))
                          .entryList({"snapshot-*.sketchyup"}, QDir::Files)
                          .size() == 1,
                  "Successful retry compacts only after durable replacement");
        }
        check(readRecovery(retryRoot, retryKey).info.revision == second.info().revision,
              "Retry acknowledges the new verified generation");
        for (const bool compact : {false, true}) {
            for (int boundary = 1; boundary <= (compact ? 6 : 3); ++boundary) {
                const auto killedRoot =
                    temporary.filePath(QString("kill-%1-%2").arg(compact).arg(boundary));
                const auto child = fork();
                check(child >= 0, "Fork fault process");
                if (child == 0) {
                    try {
                        auto work = model();
                        RecoveryWriter writer(killedRoot, QString::fromStdString(work.identity()));
                        writer.write(captureRecovery(work));
                        if (compact)
                            for (int i = 0; i < 3; ++i) {
                                work.move(1, {.1, 0, 0});
                                writer.write(captureRecovery(work));
                            }
                        work.move(1, {.2, 0, 0});
                        syncs = 0;
                        killSync = boundary;
                        writer.write(captureRecovery(work));
                        _exit(3);
                    } catch (...) {
                        _exit(4);
                    }
                }
                int status{};
                waitpid(child, &status, 0);
                check(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
                      "Writer terminated at requested sync boundary");
                const auto entries = listRecoveries(killedRoot);
                check(entries.size() == 1 && entries[0].verified && entries[0].issue.isEmpty() &&
                          entries[0].info.revision >= (compact ? 5u : 2u),
                      "Forced termination releases stale lock and recovers complete last-good "
                      "generation");
            }
        }
        // Complete M4 records, including raw asset chunks, use exactly the native codec.
        auto m4 = loadDocument(QString(SOURCE_DIR) + "/tests/fixtures/m4-complete-v11.sketchyup");
        const auto m4Root = temporary.filePath("m4");
        QString m4Key;
        {
            RecoveryWriter writer(m4Root, QString::fromStdString(m4.identity()));
            m4Key = writer.key();
            writer.write(captureRecovery(m4));
        }
        auto m4Result = readRecovery(m4Root, m4Key);
        check(m4Result.document && encodeContainer(*m4Result.document) == encodeContainer(m4),
              "Recovery preserves complete M4 identity, asset bytes and allocator floors");
        discardRecovery(root, key);
        check(listRecoveries(root).empty() && read(source) == original,
              "Explicit discard removes only the selected recovery session");
        rejects([&] { discardRecovery(root, "../explicit.sketchyup"); });
        std::cout << "Recovery snapshot isolation, checksums/chains, all tail boundaries, full "
                     "disk, sync failures, forced termination, compaction, locks, discard and "
                     "complete M4 persistence passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
