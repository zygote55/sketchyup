#include "integrations/render_queue.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write queue fixture");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read queue fixture");
    return file.readAll();
}
QString quote(QString value) { return "'" + value.replace("'", "'\\''") + "'"; }
void launcher(const QString &path, const QString &mode) {
    write(path, ("#!/bin/sh\nexec " +
                 quote(QCoreApplication::applicationDirPath() + "/blender_job_tests") + " --fake " +
                 quote(mode) + " \"$@\"\n")
                    .toUtf8());
    check(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner),
          "Executable queue fixture");
}
template <class F> void wait(F predicate, const char *message, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(predicate(), message);
}
StoredRenderJob record(const RenderQueue &queue, const QString &id) {
    for (const auto &job : queue.jobs())
        if (job.id == id)
            return job;
    throw std::runtime_error("Missing queued job");
}
std::shared_ptr<const PreparedRender> fixture(Document &document) {
    document.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    RenderOptions options;
    options.settings = {64, 64, 1, 0};
    return PreparedRender::prepare(RenderSnapshot::capture(document, options));
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().contains("--host")) {
            const auto root = app.arguments()[2], executable = app.arguments()[3];
            Document document;
            auto captured = fixture(document);
            RenderQueue queue(root);
            BlenderJob::Options options;
            options.executable = executable;
            const auto id = queue.enqueue(captured, options);
            QTimer timer;
            QObject::connect(&timer, &QTimer::timeout, &app, [&] {
                if (queue.workerProcessId(id) <= 0)
                    return;
                QDirIterator files(root + "/" + id, {"worker.log"}, QDir::Files,
                                   QDirIterator::Subdirectories);
                if (!files.hasNext())
                    return;
                if (!read(files.next()).contains("Retained worker log"))
                    return;
                write(root + "/proof.json",
                      QJsonDocument(
                          QJsonObject{{"job", id}, {"workerPid", queue.workerProcessId(id)}})
                          .toJson());
                timer.stop();
            });
            timer.start(20);
            return app.exec();
        }
        QTemporaryDir files;
        const auto hang = files.filePath("hang"), success = files.filePath("success"),
                   failure = files.filePath("failure"), fallback = files.filePath("fallback");
        launcher(hang, "hang");
        launcher(success, "success");
        launcher(failure, "fail");
        launcher(fallback, "fallback");
        Document document;
        auto captured = fixture(document);
        const auto capturedRevision = document.revision();
        const auto root = files.filePath("jobs");
        {
            RenderQueue queue(root, {4, 12, 1024LL * 1024 * 1024}, 2);
            BlenderJob::Options options;
            options.executable = hang;
            const auto first = queue.enqueue(captured, options),
                       second = queue.enqueue(captured, options);
            options.executable = success;
            const auto third = queue.enqueue(captured, options),
                       fourth = queue.enqueue(captured, options);
            queue.cancel(fourth);
            wait(
                [&] {
                    return queue.running() == 2 && queue.workerProcessId(first) > 0 &&
                           queue.workerProcessId(second) > 0;
                },
                "Two workers start concurrently");
            check(record(queue, third).state == RenderJobState::Queued &&
                      record(queue, fourth).state == RenderJobState::Canceled,
                  "Remaining work queues in order and cancels before launch");
            document.addFace({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
            const auto liveRevision = document.revision();
            queue.cancel(first);
            wait(
                [&] {
                    return record(queue, first).state == RenderJobState::Canceled &&
                           record(queue, third).state == RenderJobState::Completed;
                },
                "Free slot advances next captured job");
            check(queue.result(third)->manifest["revision"] == QString::number(capturedRevision) &&
                      document.revision() == liveRevision,
                  "Concurrent jobs preserve captured provenance and live model edits");
            queue.cancel(second);
            wait([&] { return queue.running() == 0; }, "Running cancellation drains workers");
            queue.retry(first);
            wait([&] { return queue.workerProcessId(first) > 0; }, "Retry starts captured work");
            queue.cancel(first);
            wait([&] { return queue.running() == 0; }, "Retried work cancels");
            check(record(queue, first).attempts == 2 &&
                      record(queue, first).report["runs"].toArray().size() == 2,
                  "Retry retains bounded attempt history");
            options.executable = failure;
            const auto repaired = queue.enqueue(captured, options);
            wait([&] { return record(queue, repaired).state == RenderJobState::Failed; },
                 "Worker failure is retained");
            launcher(failure, "success");
            queue.retry(repaired);
            wait([&] { return record(queue, repaired).state == RenderJobState::Completed; },
                 "Retry succeeds after executable repair");
            check(queue.result(repaired)->manifest["revision"] == QString::number(capturedRevision),
                  "Retry never recaptures current model");
            options.executable = fallback;
            options.backend = "METAL";
            options.deviceId = "unavailable";
            const auto cpu = queue.enqueue(captured, options);
            wait([&] { return record(queue, cpu).state == RenderJobState::Completed; },
                 "Queue permits explicit Cycles CPU fallback");
            check(queue.result(cpu)->manifest["cpuFallbackUsed"] == true,
                  "Retained CPU fallback verifies against captured options");
            queue.clearFinished();
            check(queue.jobs().empty(), "Explicit cleanup preserves no hidden retained jobs");
            options.executable = hang;
            options.backend = "CPU";
            options.deviceId = "CPU";
            const auto closing = queue.enqueue(captured, options);
            wait([&] { return queue.workerProcessId(closing) > 0; }, "Shutdown fixture runs");
        }
        {
            const auto failedRoot = files.filePath("failed-publication");
            RenderQueue queue(failedRoot);
            BlenderJob::Options options;
            options.executable = success;
            const auto id = queue.enqueue(captured, options);
            check(QDir().mkdir(failedRoot + "/" + id + "/image.png"),
                  "Block image publication with a directory");
            wait([&] { return record(queue, id).state == RenderJobState::Failed; },
                 "Image write failure never reports completed");
            check(!record(queue, id).message.isEmpty(), "Publication failure is explained");
            check(QDir(failedRoot + "/" + id + "/image.png").removeRecursively(),
                  "Repair image destination");
            queue.retry(id);
            wait([&] { return record(queue, id).state == RenderJobState::Completed; },
                 "Retry succeeds after storage repair");
            const auto blocked = queue.enqueue(captured, options);
            const auto metadata = failedRoot + "/" + blocked + "/job.json";
            check(QFile::remove(metadata) && QDir().mkdir(metadata), "Block record publication");
            wait([&] { return record(queue, blocked).state == RenderJobState::Failed; },
                 "Record write failure is visible even when failure cannot persist");
            check(queue.workerProcessId(blocked) == 0,
                  "Worker never launches without durable running state");
        }
        {
            RenderJobStore reopened(root);
            check(reopened.jobs().size() == 1 &&
                      reopened.jobs().begin()->second.state == RenderJobState::Interrupted,
                  "Graceful owner destruction retains interrupted work");
        }
#ifdef Q_OS_LINUX
        const auto crashRoot = files.filePath("crash-jobs");
        QProcess host;
        host.start(app.applicationFilePath(), {"--host", crashRoot, hang});
        check(host.waitForStarted(5000), "Start disposable application process");
        wait([&] { return QFileInfo::exists(crashRoot + "/proof.json"); },
             "Application captured worker PID and durable log");
        const auto proof = QJsonDocument::fromJson(read(crashRoot + "/proof.json")).object();
        const auto pid = proof["workerPid"].toInteger();
        check(pid > 0, "Valid worker PID");
        host.kill();
        check(host.waitForFinished(5000), "Force-stop disposable application");
        wait(
            [&] {
                QFile state("/proc/" + QString::number(pid) + "/stat");
                if (!state.open(QIODevice::ReadOnly))
                    return true;
                const auto value = state.readAll();
                const auto end = value.lastIndexOf(')');
                return end >= 0 && value.mid(end + 2, 1) == "Z";
            },
            "Worker dies with its application instead of running orphaned");
        RenderJobStore recovered(crashRoot);
        const auto id = proof["job"].toString();
        check(recovered.jobs().at(id).state == RenderJobState::Interrupted &&
                  QJsonDocument(recovered.jobs().at(id).report)
                      .toJson()
                      .contains("Retained worker log before interruption"),
              "Crash recovery preserves interrupted state and last worker log");
        check(QDir(crashRoot + "/" + id)
                  .entryList({"scratch-*"}, QDir::Dirs | QDir::NoDotAndDotDot)
                  .isEmpty(),
              "Crash recovery cleans only owned worker scratch files");
#endif
        std::cout << "Concurrent bounded queue, cancellation, retry, fallback, shutdown and forced "
                     "termination passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
