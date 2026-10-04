#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <cerrno>
#include <csignal>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace sketchy;
namespace {
int failSync, syncError = EIO, directorySyncs, killDirectory;
bool failWrite, killFile, failFileSync;
bool directory(int fd) {
    struct stat st{};
    return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejects(F f) {
    try {
        f();
    } catch (const std::exception &e) {
        return e.what();
    }
    throw std::runtime_error("Expected rejection");
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
QByteArray changeManifest(const QByteArray &bytes,
                          const std::function<void(QJsonObject &)> &change) {
    auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
    auto object = QJsonDocument::fromJson(bytes.mid(16, length)).object();
    change(object);
    auto manifest = QJsonDocument(object).toJson(QJsonDocument::Compact);
    auto header = bytes.first(16);
    qToLittleEndian<quint32>(manifest.size(), header.data() + 12);
    return header + manifest + bytes.mid(16 + length);
}
} // namespace
// Link-time syscall interposition confines fault injection to this test process.
extern "C" int fsync(int fd) {
    if (directory(fd)) {
        ++directorySyncs;
        if (killDirectory == directorySyncs)
            raise(SIGKILL);
        if (failSync == directorySyncs) {
            errno = syncError;
            return -1;
        }
    } else {
        if (killFile)
            raise(SIGKILL);
        if (failFileSync) {
            errno = EIO;
            return -1;
        }
    }
    return syscall(SYS_fsync, fd);
}
extern "C" ssize_t write(int fd, const void *buffer, size_t size) {
    struct stat st{};
    if (failWrite && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        errno = ENOSPC;
        return -1;
    }
    return syscall(SYS_write, fd, buffer, size);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "Temporary directory");
        for (const auto &name :
             {"container-scene-v2.sketchyup", "raw-scene-v2.json", "raw-scene-v1.json"}) {
            const auto fixture = read(QString(SKETCHYUP_TEST_FIXTURES) + "/" + name);
            auto old = decodeContainer(fixture);
            require(old.bodies().size() == 1 &&
                        !old.bodies().begin()->second->topology.edges.empty(),
                    "Historical fixture builds persistent edge IDs");
            const auto roundtrip = decodeContainer(encodeContainer(old));
            require(roundtrip.bodies().begin()->second->topology ==
                        old.bodies().begin()->second->topology,
                    "Migrated edge IDs survive current container");
        }
        for (const auto &name : {"container-topology-v3.sketchyup", "raw-topology-v3.json"}) {
            const auto fixture = read(QString(SKETCHYUP_TEST_FIXTURES) + "/" + name);
            const auto old = decodeContainer(fixture);
            require(old.bodies().size() == 3, "Historical topology fixture contexts");
            const auto migrated = decodeContainer(encodeContainer(old));
            for (const auto &[id, body] : old.bodies()) {
                require(body->curves.empty() && migrated.bodies().at(id)->curves.empty(),
                        "Migration does not invent analytic curves");
                require(*body == *migrated.bodies().at(id), "Version-3 topology IDs preserved");
            }
        }
        Document curved;
        curved.addCurve(0, centerCurve(CurveKind::Circle, {}, 2, 0, 2 * std::acos(-1), 24));
        curved.addCurve(0, twoPointArc({3, 0, 0}, {5, 0, 0}, {0, 0, 1}, .5, 12));
        const auto curvedBytes = encodeContainer(curved);
        require(encodeContainer(decodeContainer(curvedBytes)) == curvedBytes,
                "Curves exact container roundtrip");
        auto corruptCurve = [&](auto mutate) {
            auto root = QJsonDocument::fromJson(encodeDocument(curved)).object();
            auto bodies = root["bodies"].toArray();
            auto body = bodies[0].toObject();
            auto curves = body["curves"].toArray();
            auto curve = curves[0].toObject();
            mutate(curve);
            curves[0] = curve;
            body["curves"] = curves;
            bodies[0] = body;
            root["bodies"] = bodies;
            try {
                decodeDocument(QJsonDocument(root).toJson());
            } catch (const std::exception &) {
                return;
            }
            throw std::runtime_error("Malformed curve accepted");
        };
        corruptCurve([](auto &c) { c["radius"] = 3; });
        corruptCurve([](auto &c) { c["id"] = "1"; });
        corruptCurve([](auto &c) { c["segments"] = 1.5; });
        corruptCurve([](auto &c) { c["edges"] = QJsonArray{}; });
        corruptCurve([](auto &c) { c["kind"] = "spline"; });
        Document doc;
        auto id = doc.addFace({{{0, 0, 0}, {3, 0, 0}, {0, 2, 0}}});
        auto bytes = encodeContainer(doc);
        require(bytes.startsWith(QByteArray("SKUPDOC\0", 8)), "Binary envelope");
        require(encodeContainer(decodeContainer(bytes)) == bytes, "Exact container roundtrip");
        require(encodeDocument(decodeContainer(encodeDocument(doc))) == encodeDocument(doc),
                "Raw current-schema roundtrip");
        auto legacy = QJsonDocument::fromJson(encodeDocument(doc)).object();
        legacy["version"] = 1;
        legacy.remove("revision");
        auto records = legacy["bodies"].toArray();
        for (int i = 0; i < records.size(); ++i) {
            auto body = records[i].toObject();
            for (const auto &key :
                 {"parent", "transform", "properties", "nextEdgeId", "edges", "curves"})
                body.remove(key);
            records[i] = body;
        }
        legacy["bodies"] = records;
        auto legacyBytes = QJsonDocument(legacy).toJson();
        auto path = dir.filePath("model.sketchyup");
        write(path, legacyBytes);
        auto migrated = loadDocument(path);
        require(read(path) == legacyBytes && migrated.identity() == doc.identity(),
                "Migration leaves source intact");
        saveDocument(migrated, path);
        require(read(path + ".bak") == legacyBytes, "First migration save preserves legacy backup");
        for (qsizetype i = 0; i < bytes.size(); ++i)
            rejects([&] { decodeContainer(bytes.first(i)); });
        auto corrupt = bytes;
        corrupt.back() ^= 1;
        rejects([&] { decodeContainer(corrupt); });
        corrupt = bytes;
        qToLittleEndian<quint32>(999, corrupt.data() + 8);
        rejects([&] { decodeContainer(corrupt); });
        corrupt = bytes;
        qToLittleEndian<quint32>(UINT32_MAX, corrupt.data() + 12);
        rejects([&] { decodeContainer(corrupt); });
        rejects([&] { decodeContainer(bytes + "extra"); });
        for (const auto &key :
             {"documentId", "epoch", "revision", "units", "allocatorFloors", "requiredFeatures"})
            rejects([&] {
                decodeContainer(changeManifest(bytes, [&](auto &m) { m[key] = "invalid"; }));
            });
        for (const auto &key : {"offset", "bytes", "sha256", "kind", "encoding"})
            rejects([&] {
                decodeContainer(changeManifest(bytes, [&](auto &m) {
                    auto chunks = m["chunks"].toArray();
                    auto c = chunks[0].toObject();
                    c[key] = "18446744073709551615";
                    chunks[0] = c;
                    m["chunks"] = chunks;
                }));
            });
        rejects([&] {
            decodeContainer(changeManifest(bytes, [](auto &m) {
                auto chunks = m["chunks"].toArray();
                chunks.append(chunks[0]);
                m["chunks"] = chunks;
            }));
        });
        rejects([&] {
            decodeContainer(changeManifest(bytes, [](auto &m) { m["futureRequiredData"] = true; }));
        });
        // Save completion belongs to captured content, including branching copies and reopen.
        auto snapshot = captureSave(doc);
        auto branch = doc;
        doc.move(id, {1, 0, 0});
        branch.move(id, {2, 0, 0});
        auto branchSnapshot = captureSave(branch);
        saveSnapshot(doc, snapshot, path);
        require(doc.dirty() && read(path) == bytes, "Later edits remain dirty");
        doc.undo();
        require(!doc.dirty(), "Undo to saved content is clean");
        doc.redo();
        saveSnapshot(doc, branchSnapshot, path);
        require(doc.dirty(),
                "Independent copy with same revision cannot falsely acknowledge current content");
        Document unrelated;
        rejects([&] { saveSnapshot(unrelated, snapshot, path); });
        auto reopened = loadDocument(path);
        rejects([&] { saveSnapshot(reopened, snapshot, path); });
        saveDocument(doc, path);
        require(!doc.dirty(), "Durable save acknowledged");
        const auto previous = read(path);
        doc.move(id, {0, 1, 0});
        const auto next = encodeContainer(doc);
        // Every process-kill boundary leaves a complete old/new target and, after replacement,
        // a verified old backup. A kill before target rename cannot destroy the old target.
        for (int stage = 0; stage < 3; ++stage) {
            write(path, previous);
            const auto pid = fork();
            require(pid >= 0, "fork");
            if (!pid) {
                directorySyncs = 0;
                killFile = stage == 0;
                killDirectory = stage;
                saveDocument(doc, path);
                _exit(90);
            }
            int status = 0;
            require(waitpid(pid, &status, 0) == pid, "waitpid");
            require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
                    "Injected kill reached boundary");
            require(read(path) == (stage == 2 ? next : previous), "Kill preserves complete target");
            if (stage > 0)
                require(read(path + ".bak") == previous, "Kill preserves last good backup");
            (void)loadDocument(path);
        }
        for (int stage : {1, 2}) {
            write(path, previous);
            directorySyncs = 0;
            failSync = stage;
            auto message = rejects([&] { saveDocument(doc, path); });
            failSync = 0;
            require(doc.dirty(), "Failed sync remains dirty");
            require(read(path) == (stage == 2 ? next : previous),
                    "Sync failure reports actual replacement state");
            require((message.find("uncertain") != std::string::npos) == (stage == 2),
                    "Post-rename uncertainty is explicit");
        }
        write(path, previous);
        failWrite = true;
        rejects([&] { saveDocument(doc, path); });
        failWrite = false;
        require(read(path) == previous && doc.dirty(),
                "Disk-full write preserves target and dirty state");
        failFileSync = true;
        rejects([&] { saveDocument(doc, path); });
        failFileSync = false;
        require(read(path) == previous && doc.dirty(),
                "Disconnected storage sync preserves target");
        const auto locked = dir.filePath("locked");
        require(QDir().mkdir(locked), "Permission fixture directory");
        const auto lockedPath = locked + "/model.sketchyup";
        write(lockedPath, previous);
        require(::chmod(QFile::encodeName(dir.path()).constData(), 0755) == 0 &&
                    ::chmod(QFile::encodeName(locked).constData(), 0555) == 0,
                "Fixture permissions");
        const auto child = fork();
        require(child >= 0, "Permission fork");
        if (!child) {
            if (geteuid() == 0 && setuid(65534) != 0)
                _exit(91);
            try {
                saveDocument(doc, lockedPath);
                _exit(92);
            } catch (const std::exception &) {
                _exit(doc.dirty() ? 0 : 93);
            }
        }
        int permissionStatus = 0;
        require(waitpid(child, &permissionStatus, 0) == child && WIFEXITED(permissionStatus) &&
                    WEXITSTATUS(permissionStatus) == 0,
                "Permission denial is reported without acknowledging save");
        require(read(lockedPath) == previous, "Permission denial preserves previous file");
        require(::chmod(QFile::encodeName(locked).constData(), 0755) == 0,
                "Restore fixture permissions");
        // A backup obstruction fails before target replacement; existing good backup survives
        // replacement of an invalid file explicitly selected by the user.
        const auto blocked = dir.filePath("blocked.sketchyup");
        write(blocked, previous);
        require(QDir().mkdir(blocked + ".bak"), "Backup obstruction");
        rejects([&] { saveDocument(doc, blocked); });
        require(read(blocked) == previous, "Backup failure preserves target");
        write(path + ".bak", previous);
        write(path, "invalid existing document");
        saveDocument(doc, path);
        require(read(path + ".bak") == previous, "Invalid target never overwrites verified backup");
        const auto edge = doc.bodies().at(id)->topology.edges.begin()->first;
        doc.splitEdge(id, edge, .5);
        const auto edgeFloor = doc.bodies().at(id)->topology.nextId;
        doc.undo();
        auto restoredTopology = decodeContainer(encodeContainer(doc));
        require(restoredTopology.bodies().at(id)->topology == doc.bodies().at(id)->topology,
                "Exact edge records and allocator survive save after undo");
        restoredTopology.splitEdge(id, edge, .4);
        for (const auto &[newId, record] : restoredTopology.bodies().at(id)->topology.edges)
            require(doc.bodies().at(id)->topology.edges.contains(newId) || newId >= edgeFloor,
                    "Reopened edge split never reuses retired IDs");
        auto invalid = QJsonDocument::fromJson(encodeDocument(doc)).object();
        auto invalidBodies = invalid["bodies"].toArray();
        auto invalidBody = invalidBodies[0].toObject();
        invalidBody["nextEdgeId"] = "1";
        invalidBodies[0] = invalidBody;
        invalid["bodies"] = invalidBodies;
        rejects([&] { decodeDocument(QJsonDocument(invalid).toJson()); });
        // A symlink save syncs/replaces its actual target and leaves the link intact.
        const auto link = dir.filePath("alias.sketchyup");
        require(QFile::link(path, link), "Symlink fixture");
        doc.move(id, {0, 0, 1});
        saveDocument(doc, link);
        require(QFileInfo(link).isSymLink() && read(path) == encodeContainer(doc),
                "Symlink target save");
        std::cout
            << "Container, snapshot, migration, ENOSPC, fsync and process-kill checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        failWrite = false;
        failSync = 0;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
