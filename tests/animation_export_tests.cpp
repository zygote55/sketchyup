#include "core/scenes.hpp"
#include "integrations/animation_export.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read animation artifact");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
QString quote(QString s) { return "'" + s.replace("'", "'\\''") + "'"; }
void wait(AnimationExport &job) {
    QElapsedTimer timer;
    timer.start();
    while (!job.done() && timer.elapsed() < 120000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(job.done(), "Animation finishes within deadline");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool real = app.arguments().contains("--real");
    if (real && qEnvironmentVariableIsEmpty("SKETCHYUP_BLENDER_TEST"))
        return 77;
    try {
        QTemporaryDir files;
        check(files.isValid(), "Animation fixture directory");
        auto launcher = [&](const QString &name, const QString &mode) {
            const auto path = files.filePath(name);
            write(path,
                  ("#!/bin/sh\nexec " + quote(app.applicationDirPath() + "/blender_job_tests") +
                   " --fake " + quote(mode) + " \"$@\"\n")
                      .toUtf8());
            check(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                  QFileDevice::ExeOwner),
                  "Executable fixture");
            return path;
        };
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        SceneSnapshot a;
        a.camera = SceneCamera{};
        const auto first = createScene(doc, "A", a);
        a.camera->yaw = 90;
        const auto second = createScene(doc, "B", a);
        RenderOptions settings;
        settings.settings = {64, 64, 1, 0};
        auto capture = AnimationCapture::capture(doc, {first, second}, {24, 2, 0}, settings);
        const auto before = encodeDocument(doc);
        const auto history = doc.history().total;
        BlenderJob::Options options;
        options.executable =
            real ? qEnvironmentVariable("SKETCHYUP_BLENDER_TEST") : launcher("success", "success");
        AnimationExport full;
        const auto output = files.filePath("complete");
        full.start(capture, output, options);
        wait(full);
        check(full.state() == AnimationExport::State::Completed && full.completedFrames() == 3,
              "All frame jobs publish");
        const auto manifest = QJsonDocument::fromJson(read(output + "/animation.json")).object();
        check(manifest["state"] == "completed" && manifest["framesPerSecond"] == 24 &&
                  manifest["frames"].toArray().size() == 3,
              "Final manifest contains exact frame count and timing");
        for (const auto &entry : manifest["frames"].toArray()) {
            const auto record = entry.toObject();
            const auto png = read(output + "/" + record["file"].toString());
            check(QString::fromLatin1(
                      QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex()) ==
                      record["sha256"].toString(),
                  "Published frame hash matches bytes");
            const auto detail =
                QJsonDocument::fromJson(read(output + "/" + record["manifest"].toString()))
                    .object();
            check(detail["result"].isObject() && detail["camera"].isObject(),
                  "Per-frame transfer verification and camera retained");
        }
        if (real) {
            check(encodeDocument(doc) == before && doc.history().total == history,
                  "Real Blender frames preserve live model");
            std::cout << "Real Blender animation frame sequence verified\n";
            return 0;
        }
        AnimationExport interrupted;
        auto hanging = options;
        hanging.executable = launcher("hang", "hang");
        interrupted.start(capture, files.filePath("interrupted"), hanging);
        QElapsedTimer launchWait;
        launchWait.start();
        auto launched = [&] {
            const auto *worker = interrupted.findChild<BlenderJob *>();
            return worker && worker->phase() == BlenderJob::Phase::Rendering &&
                   worker->processId() > 0;
        };
        while (!launched() && launchWait.elapsed() < 10000) {
            QCoreApplication::processEvents();
            QThread::msleep(2);
        }
        check(launched(), "Animation worker starts before cancellation");
        interrupted.cancel();
        wait(interrupted);
        check(interrupted.state() == AnimationExport::State::Canceled &&
                  interrupted.completedFrames() == 0,
              "Cancel running worker publishes no partial frame");
        AnimationExport canceled;
        QObject::connect(&canceled, &AnimationExport::changed, &app, [&] {
            if (canceled.completedFrames() == 1 &&
                canceled.state() == AnimationExport::State::Running)
                canceled.cancel();
        });
        canceled.start(capture, files.filePath("canceled"), options);
        wait(canceled);
        check(canceled.state() == AnimationExport::State::Canceled &&
                  canceled.completedFrames() == 1 &&
                  QFileInfo::exists(files.filePath("canceled/frame-0001.png")),
              "Cancellation preserves finished frame");
        check(QJsonDocument::fromJson(read(files.filePath("canceled/animation.json")))
                      .object()["state"] == "canceled",
              "Cancellation persists incomplete outcome");
        AnimationExport blocked;
        QObject::connect(&blocked, &AnimationExport::changed, &app, [&] {
            if (blocked.completedFrames() == 1)
                QDir().mkdir(files.filePath("blocked/frame-0002.png"));
        });
        blocked.start(capture, files.filePath("blocked"), options);
        wait(blocked);
        check(blocked.state() == AnimationExport::State::Failed && blocked.completedFrames() == 1 &&
                  QFileInfo::exists(files.filePath("blocked/frame-0001.png")),
              "Output failure never reports completed animation or removes finished frame");
        AnimationExport immediate;
        immediate.start(capture, files.filePath("immediate"), options);
        immediate.cancel();
        wait(immediate);
        check(immediate.state() == AnimationExport::State::Canceled &&
                  immediate.completedFrames() == 0,
              "Cancel during immutable preparation");
        AnimationExport failed;
        options.executable = launcher("failure", "fail");
        failed.start(capture, files.filePath("failed"), options);
        wait(failed);
        check(failed.state() == AnimationExport::State::Failed && failed.completedFrames() == 0,
              "Failed worker does not publish a frame");
        bool rejected = false;
        try {
            AnimationExport existing;
            existing.start(capture, output, options);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && read(output + "/animation.json") == QJsonDocument(manifest).toJson(),
              "Existing batch cannot be overwritten");
        check(encodeDocument(doc) == before && doc.history().total == history,
              "All batch outcomes preserve live document and history");
        std::cout
            << "Immutable frame jobs, cancellation, retained output and storage failure passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
