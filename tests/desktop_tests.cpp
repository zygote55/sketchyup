#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <csignal>
#include <iostream>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace sketchy;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Expected rejection");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document d;
        auto id = d.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                             {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        d.extrude(id, d.bodies().at(id)->surface.faces.begin()->first, 2);
        auto bytes = encodeDocument(d);
        auto copy = decodeDocument(bytes);
        require(encodeDocument(copy) == bytes, "Exact identity and topology roundtrip");
        require(!copy.dirty(), "Loaded state clean");
        QTemporaryDir dir;
        require(dir.isValid(), "Temporary directory");
        auto path = dir.filePath("model.sketchyup");
        saveDocument(d, path);
        require(!d.dirty(), "Save state");
        d.move(id, {1, 0, 0});
        rejects([&] { saveDocument(d, dir.filePath("missing/model.sketchyup")); });
        require(d.dirty(), "Failed save remains dirty");
        require(encodeDocument(loadDocument(path)) == bytes, "Failed save preserves previous file");
        saveDocument(d, path);
        require(encodeDocument(loadDocument(path)) == encodeDocument(d), "Atomic replacement");
        rejects([&] { decodeDocument(bytes.left(bytes.size() / 2)); });
        auto root = QJsonDocument::fromJson(bytes).object();
        root["version"] = 2;
        rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        root["version"] = 1.5;
        rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        root["version"] = 1;
        root["nextId"] = "1";
        rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        root["nextId"] = "2";
        root["bodies"] = true;
        rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        rejects([&] { decodeDocument(QByteArray(32 * 1024 * 1024 + 1, ' ')); });
        // Force a short write without filling the developer's disk.
        auto beforeFailure = encodeDocument(loadDocument(path));
        auto child = fork();
        require(child >= 0, "fork");
        if (child == 0) {
            std::signal(SIGXFSZ, SIG_IGN);
            rlimit limit{64, 64};
            if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
                _exit(3);
            try {
                saveDocument(d, path);
                _exit(2);
            } catch (const std::exception &) {
                _exit(0);
            }
        }
        int status = 0;
        waitpid(child, &status, 0);
        require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Short write is reported as failure");
        require(encodeDocument(loadDocument(path)) == beforeFailure,
                "Short write preserves previous file");
        Document batch;
        QJsonObject create{
            {"command", "geometry.face"},
            {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                            QJsonArray{2, 2, 0}, QJsonArray{0, 2, 0}}}}};
        auto request = [&](QJsonArray commands) {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(batch.identity())},
                               {"expectedRevision", QString::number(batch.revision())},
                               {"commands", commands}};
        };
        auto pristine = encodeDocument(batch);
        rejects([&] {
            executeBatch(batch, request({create, QJsonObject{{"command", "unavailable"}}}));
        });
        require(encodeDocument(batch) == pristine && batch.revision() == 0,
                "Failed batch commits nothing");
        executeBatch(batch, request({create, QJsonObject{{"command", "geometry.extrude_isolated"},
                                                         {"body", "1"},
                                                         {"face", "5"},
                                                         {"distance", 3}}}));
        require(batch.revision() == 1 && batch.bodies().at(1)->surface.faces.size() == 6,
                "Batch commits once");
        batch.undo();
        require(batch.bodies().empty(), "Entire batch is one undo step");
        batch.redo();
        require(batch.bodies().size() == 1, "Batch redo");
        auto stale = request({create});
        stale["expectedRevision"] = "0";
        rejects([&] { executeBatch(batch, stale); });
        auto wrong = request({create});
        wrong["documentId"] = "another-document";
        rejects([&] { executeBatch(batch, wrong); });
        executeBatch(batch,
                     request({create, QJsonObject{{"command", "geometry.delete"}, {"body", "2"}},
                              QJsonObject{{"command", "geometry.translate"},
                                          {"body", "1"},
                                          {"delta", QJsonArray{1, 0, 0}}}}));
        require(batch.nextId() == 3, "IDs created and removed in a batch remain retired");
        std::cout << "Persistence roundtrip, failed save, corruption, schema and resource-limit "
                     "checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
