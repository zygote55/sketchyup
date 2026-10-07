#include "integrations/render_store.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid render store action accepted");
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write store fixture");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read store fixture");
    return file.readAll();
}
void wait(BlenderJob &job) {
    QElapsedTimer timer;
    timer.start();
    while (!job.done() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(job.done(), "Bounded test worker completes");
}
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        const auto executable = files.filePath("blender");
        write(executable,
              ("#!/bin/sh\nexec " + quote(app.applicationDirPath() + "/blender_job_tests") +
               " --fake success \"$@\"\n")
                  .toUtf8());
        check(QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                    QFileDevice::ExeOwner),
              "Executable test worker");
        BlenderJob::Options options;
        options.executable = executable;
        Document document;
        document.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        RenderOptions render;
        render.settings = {64, 64, 1, 0};
        auto captured = PreparedRender::prepare(RenderSnapshot::capture(document, render));
        const auto source = captured->manifest();
        const auto path = files.filePath("jobs");
        QString completed, interrupted;
        const RenderJobStore::Limits limits{2, 4, 512LL * 1024 * 1024};
        QByteArray expectedImage;
        {
            RenderJobStore store(path, limits);
            rejects([&] { RenderJobStore duplicate(path); });
            completed = store.enqueue(*captured, options);
            interrupted = store.enqueue(*captured, options);
            check(store.jobs().at(completed).queueSequence <
                      store.jobs().at(interrupted).queueSequence,
                  "Persistent queue order does not depend on wall-clock resolution");
            rejects([&] { store.enqueue(*captured, options); });
            rejects([&] { store.remove(completed); });
            document.addFace({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
            captured.reset();
            check(store.input(completed)->manifest() == source,
                  "Stored input survives original destruction and live edits");
            store.transition(completed, RenderJobState::Running, {},
                             {{"log", QString(1024 * 1024, 'x')}});
            check(store.jobs().at(completed).report.value("diagnosticsTruncated") == true,
                  "Oversized diagnostics are bounded without losing the state transition");
            BlenderJob job;
            job.start(store.input(completed), options);
            wait(job);
            check(bool(job.result()), "Verified fixture result");
            store.complete(completed, *job.result(), job.report());
            expectedImage = job.result()->png;
            check(store.result(completed)->png == expectedImage &&
                      store.jobs().at(completed).attempts == 1,
                  "Completed result is retained and reverified");
            rejects([&] { store.retry(completed); });
            store.transition(interrupted, RenderJobState::Running);
            store.transition(interrupted, RenderJobState::Canceling);
        }
        {
            RenderJobStore store(path, limits);
            check(store.jobs().at(interrupted).state == RenderJobState::Interrupted &&
                      store.jobs().at(completed).state == RenderJobState::Completed &&
                      store.result(completed)->png == expectedImage,
                  "Restart reconciles unfinished work and verifies retained pixels");
            const auto originalOrder = store.jobs().at(interrupted).queueSequence;
            store.retry(interrupted);
            check(store.jobs().at(interrupted).queueSequence > originalOrder,
                  "Retried work appends to the persistent queue order");
            check(store.jobs().at(interrupted).state == RenderJobState::Queued &&
                      store.input(interrupted)->manifest() == source,
                  "Retry reuses immutable original input");
            const auto record = path + "/" + interrupted + "/job.json";
            const auto previous = read(record);
            check(QFile::remove(record) && QDir().mkdir(record),
                  "Simulate atomic publication failure");
            rejects([&] { store.transition(interrupted, RenderJobState::Running); });
            check(store.jobs().at(interrupted).state == RenderJobState::Queued,
                  "Write failure cannot publish running state");
            check(QDir().rmdir(record), "Remove blocked destination");
            write(record, previous);
            store.transition(interrupted, RenderJobState::Canceled);
            store.clearFinished();
            check(store.jobs().empty(), "Explicit cleanup removes only terminal jobs");
        }
        captured = PreparedRender::prepare(RenderSnapshot::capture(document, render));
        {
            RenderJobStore small(files.filePath("small"), {1, 1, 195LL * 1024 * 1024});
            rejects([&] { small.enqueue(*captured, options); });
            check(small.jobs().empty(), "Output reservation prevents over-quota enqueue");
        }
        QString damaged;
        {
            RenderJobStore store(path, limits);
            damaged = store.enqueue(*captured, options);
            store.transition(damaged, RenderJobState::Running);
            BlenderJob job;
            job.start(store.input(damaged), options);
            wait(job);
            check(bool(job.result()), "Second verified fixture result");
            auto bad = *job.result();
            bad.png = "broken PNG";
            rejects([&] { store.complete(damaged, bad, {}); });
            check(store.jobs().at(damaged).state == RenderJobState::Running,
                  "Invalid pixels never mark a job complete");
            store.complete(damaged, *job.result(), job.report());
            write(path + "/" + damaged + "/image.png", QByteArray("corrupted after completion"));
        }
        {
            RenderJobStore store(path, limits);
            check(store.jobs().at(damaged).state == RenderJobState::Failed,
                  "Restart does not trust a completed flag with corrupt output");
            rejects([&] { store.result(damaged); });
            store.retry(damaged);
            store.transition(damaged, RenderJobState::Canceled);
            store.remove(damaged);
            const auto id = store.enqueue(*captured, options);
            const auto scene = path + "/" + id + "/scene/scene.glb";
            const auto original = read(scene);
            write(scene, original.left(original.size() - 1));
            rejects([&] { store.input(id); });
            store.transition(id, RenderJobState::Failed, "Corrupt test input");
            const auto external = files.filePath("external");
            check(QDir().mkdir(external), "External directory");
            write(external + "/sentinel", "keep");
            check(QDir(path + "/" + id).removeRecursively() &&
                      QFile::link(external, path + "/" + id),
                  "Replace owned directory with symlink fixture");
            rejects([&] { store.remove(id); });
            check(read(external + "/sentinel") == "keep",
                  "Cleanup cannot follow a replaced job directory");
            QFile::remove(path + "/" + id);
        }
        std::cout << "Durable render inputs/results, restart reconciliation, retries, quotas and "
                     "atomic failures passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
