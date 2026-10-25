#include "io/ifc_worker_run.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

namespace sketchy {

// Runs the helper once with a fixed argv and a scrubbed environment, in its own session, with a deadline; the
// parent ends it (terminate, then kill after a second) on cancel, on the deadline or when it floods its output.
IfcWorkerRun runIfcWorker(const QString &worker, const QString &directory, const QStringList &args, const IfcWorkerOptions &options) {
    IfcWorkerRun run;
    QProcess process;
    process.setProgram(worker);
    process.setArguments(args);
    process.setWorkingDirectory(directory);
    QProcessEnvironment environment;
    environment.insert("LC_ALL", "C.UTF-8");
    process.setProcessEnvironment(environment);
    process.setChildProcessModifier([] {
        ::setsid();
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
    });
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(10000)) {
        run.reason = "start";
        run.logTail = process.errorString();
        return run;
    }
    QElapsedTimer clock;
    clock.start();
    QByteArray pending, errors;
    bool killed = false, cancelled = false, timedOut = false, flooded = false;
    const auto readLines = [&] {
        errors += process.readAllStandardError();
        if (errors.size() > 16 * 1024)
            errors = errors.right(8 * 1024);
        pending += process.readAllStandardOutput();
        if (pending.size() > 64 * 1024 && pending.indexOf('\n') < 0) {
            flooded = true;
            pending.clear();
        }
        qsizetype at;
        while ((at = pending.indexOf('\n')) >= 0) {
            const auto line = QString::fromUtf8(pending.left(at)).trimmed();
            pending.remove(0, at + 1);
            if (line.startsWith("R "))
                run.result = QJsonDocument::fromJson(line.mid(2).toUtf8()).object();
        }
    };
    while (process.state() != QProcess::NotRunning) {
        if (!process.waitForReadyRead(50))
            process.waitForFinished(1);
        readLines();
        if (!killed && options.cancelled && options.cancelled()) {
            cancelled = true;
            process.terminate();
            killed = true;
            clock.restart();
        } else if (!killed && flooded) {
            process.terminate();
            killed = true;
            clock.restart();
        } else if (!killed && clock.elapsed() > qint64(options.deadlineSeconds) * 1000) {
            timedOut = true;
            process.terminate();
            killed = true;
            clock.restart();
        } else if (killed && clock.elapsed() > 1000 && process.state() != QProcess::NotRunning) {
            process.kill();
            clock.restart();
        }
    }
    readLines();
    errors += process.readAllStandardError();
    run.logTail = QString::fromUtf8(errors).trimmed().right(2048);
    run.exitCode = process.exitCode();
    if (cancelled) {
        run.kind = IfcWorkerRun::Cancelled;
    } else if (timedOut) {
        run.reason = "timeout";
    } else if (flooded) {
        run.reason = "error";
    } else if (process.exitStatus() == QProcess::NormalExit && run.exitCode == 0 && run.result["status"] == "ok") {
        run.kind = IfcWorkerRun::Finished;
    } else if (process.exitStatus() == QProcess::NormalExit && run.exitCode == 3 && run.result["status"] == "refused") {
        run.kind = IfcWorkerRun::Refused;
    } else {
        run.reason = process.exitStatus() == QProcess::CrashExit ? "crash"
                     : (run.logTail.contains("bad_alloc") || run.logTail.contains("length_error")) ? "memory"
                                                                                                   : "error";
    }
    return run;
}

} // namespace sketchy
