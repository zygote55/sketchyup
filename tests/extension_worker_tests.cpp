#include "automation/extension_worker.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected worker rejection");
}
QString helper(QTemporaryDir &directory, const QString &name, const QByteArray &body) {
    const auto path = directory.filePath(name);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "Worker fixture file");
    const auto bytes = QByteArray("#!/bin/sh\n") + body + '\n';
    check(file.write(bytes) == bytes.size(), "Worker fixture bytes");
    check(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner),
          "Worker fixture executable");
    return path;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QFile sample(QStringLiteral(SOURCE_DIR) + "/examples/extensions/panel.sketchyext");
        check(sample.open(QIODevice::ReadOnly), "Read worker sample");
        const auto manifest = parseExtensionManifest(sample.readAll());
        const QJsonObject parameters{{"width", 7}, {"height", 3}};
        check(runExtensionWorker(manifest, "create-panel", parameters) ==
                  resolveExtensionAction(manifest, "create-panel", parameters),
              "Real installed helper resolves exact typed public command batch");
        rejects([&] { runExtensionWorker(manifest, "create-panel", {{"width", -1}}); });
        rejects([&] { runExtensionWorker(manifest, "absent", {}); });
        rejects([&] { resolveExtensionWorkerRequest("{}"); });
        rejects([&] {
            resolveExtensionWorkerRequest(QByteArray(extensionWorkerRequestLimit + 1, ' '));
        });
        QTemporaryDir scratch;
        const auto failure = helper(scratch, "failure", "exit 7");
        const auto killed = helper(scratch, "killed", "kill -KILL $$");
        const auto malformed = helper(scratch, "malformed", "printf 'invalid'");
        const auto flood = helper(scratch, "flood", "exec /usr/bin/head -c 110000 /dev/zero");
        const auto errorFlood = helper(scratch, "stderr", "/usr/bin/head -c 10000 /dev/zero >&2");
        const auto sleeper = helper(scratch, "sleep", "exec /usr/bin/sleep 3");
        for (const auto &path :
             {failure, killed, malformed, flood, errorFlood, scratch.filePath("absent")})
            rejects([&] { runExtensionWorker(manifest, "create-panel", {}, {path, 1000}); });
        QElapsedTimer timer;
        timer.start();
        rejects([&] { runExtensionWorker(manifest, "create-panel", {}, {sleeper, 30}); });
        check(timer.elapsed() < 1500, "Timeout terminates the worker promptly");
        std::atomic_bool canceled{};
        auto *thread = QThread::create([&] {
            try {
                runExtensionWorker(manifest, "create-panel", {}, {sleeper, 5000});
            } catch (const std::exception &) {
                canceled = true;
            }
        });
        thread->start();
        QThread::msleep(30);
        thread->requestInterruption();
        check(thread->wait(1500), "Cancellation terminates the worker promptly");
        delete thread;
        check(canceled, "Interrupted worker returns no executable commands");
        const auto request =
            QJsonDocument(QJsonObject{{"protocol", 1},
                                      {"manifest", QString::fromLatin1(manifest.source.toBase64())},
                                      {"action", "create-panel"},
                                      {"parameters", QJsonObject{}}})
                .toJson(QJsonDocument::Compact);
        auto reply = QJsonDocument::fromJson(resolveExtensionWorkerRequest(request)).object();
        reply["commands"] = QJsonArray{QJsonObject{{"command", "document.units"}, {"units", "mm"}}};
        const auto outputPath = scratch.filePath("forged.json");
        QFile output(outputPath);
        check(output.open(QIODevice::WriteOnly), "Forged worker fixture");
        output.write(QJsonDocument(reply).toJson());
        output.close();
        const auto forged =
            helper(scratch, "forged", "exec /usr/bin/cat '" + outputPath.toUtf8() + "'");
        rejects([&] { runExtensionWorker(manifest, "create-panel", {}, {forged, 1000}); });
        std::cout << "Extension worker: real helper, exact output, failure, crash, timeout, "
                     "cancellation and bounded malformed output passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
