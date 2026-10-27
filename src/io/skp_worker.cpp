#include "io/skp_worker.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <vector>

#include "io/skp_package.hpp"

namespace sketchy {
namespace {

constexpr qint64 maxFileBytes = 256LL * 1024 * 1024; // ADR 0176 section 6: the input file
constexpr qint64 maxPackageBytes = 32 + qint64(SkpPackageLimits::manifestBytes) +
                                   qint64(SkpPackageLimits::geometryBytes) +
                                   qint64(SkpPackageLimits::textureChunkBytes);
constexpr qint64 maxLogBytes = 2048;
const char *const privatePrefix = "sketchyup-skp-";
// The helper's exit code for a layer of its sandbox that could not be installed (ADR 0183 section
// 7). It is a refusal, like exit 3, and it names the layer in its R line.
constexpr int sandboxUnavailableExit = 70;

[[noreturn]] void refuse(const std::string &code, const std::string &message, QJsonObject report = {}) {
    throw IfcImportError(code, message, std::move(report));
}
QString clipped(QString text, int characters) { return text.size() > characters ? text.left(characters) : text; }

// Removes private directories an earlier run left behind (the application was killed): older than a day,
// owned by us, never a symbolic link.
void sweepStale(const QString &root) {
    const QDir dir(root);
    for (const auto &entry : dir.entryInfoList({QString(privatePrefix) + "*"}, QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot)) {
        if (entry.isSymLink() || entry.ownerId() != ::getuid())
            continue;
        if (entry.lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 3600)
            QDir(entry.absoluteFilePath()).removeRecursively();
    }
}

struct RunResult {
    enum Kind { Finished, Refused, Stopped, Cancelled } kind{Stopped};
    QString reason; // for Stopped: crash, timeout, memory, error, flood, start
    QJsonObject result; // the "R" line
    QString phase;
    QString logTail;
    int exitCode{-1};
};

QString shortLog(QString text) {
    text = text.trimmed();
    if (text.size() > maxLogBytes)
        text = text.right(maxLogBytes);
    return text;
}

RunResult runOnce(const QString &worker, const QString &directory, const QString &input, const QString &output,
                  const SkpWorkerOptions &options) {
    RunResult run;
    QStringList args{"--import", input, "--output", output};
    if (options.addressSpaceBytes > 0)
        args << "--rlimit-as" << QString::number(options.addressSpaceBytes);
    args << "--rlimit-cpu" << QString::number(qint64(options.deadlineSeconds) * 4) << "--rlimit-fsize"
         << QString::number(qint64(512) * 1024 * 1024) << "--deadline-seconds" << QString::number(options.deadlineSeconds);
    QProcess process;
    process.setProgram(worker);
    process.setArguments(args);
    process.setWorkingDirectory(directory);
    QProcessEnvironment environment;
    environment.insert("LC_ALL", "C.UTF-8");
    process.setProcessEnvironment(environment);
    process.setChildProcessModifier([] {
        // Its own session, and it dies with the application.
        ::setsid();
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
    });
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(10000)) {
        run.kind = RunResult::Stopped;
        run.reason = "start";
        run.logTail = process.errorString();
        return run;
    }
    QElapsedTimer clock;
    clock.start();
    QByteArray pending, errors;
    bool killed = false, cancelled = false, timedOut = false, flooded = false;
    const auto readLines = [&] {
        // The helper's diagnostics are kept (the tail only) so a full pipe never stops it.
        errors += process.readAllStandardError();
        if (errors.size() > 16 * 1024)
            errors = errors.right(8 * 1024);
        pending += process.readAllStandardOutput();
        if (pending.size() > 64 * 1024 && pending.indexOf('\n') < 0) {
            // Nothing the helper prints is a line this long: it is not behaving, so it is stopped.
            flooded = true;
            pending.clear();
        }
        qsizetype at;
        while ((at = pending.indexOf('\n')) >= 0) {
            const auto line = QString::fromUtf8(pending.left(at)).trimmed();
            pending.remove(0, at + 1);
            if (line.startsWith("P phase ")) {
                run.phase = line.mid(8);
                if (options.progress)
                    options.progress(run.phase, 0);
            } else if (line.startsWith("P done ")) {
                if (options.progress)
                    options.progress("done", line.mid(7).toLongLong());
            } else if (line.startsWith("R ")) {
                run.result = QJsonDocument::fromJson(line.mid(2).toUtf8()).object();
            }
            if (pending.size() > 64 * 1024)
                pending.clear(); // a line that long is not ours
        }
    };
    while (process.state() != QProcess::NotRunning) {
        // Returns as soon as there is output (so a flood is seen after one chunk), or after 50 ms.
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
            process.kill(); // terminate, then kill after one second
            clock.restart();
        }
    }
    // The process's exit can be observed while its final output is still in flight; wait for the
    // delivery to finish so the last line (the R result) is not lost.
    process.waitForFinished(2000);
    readLines();
    errors += process.readAllStandardError();
    run.logTail = shortLog(QString::fromUtf8(errors));
    run.exitCode = process.exitCode();
    if (cancelled) {
        run.kind = RunResult::Cancelled;
        return run;
    }
    if (timedOut) {
        run.kind = RunResult::Stopped;
        run.reason = "timeout";
        return run;
    }
    if (flooded) {
        run.kind = RunResult::Stopped;
        run.reason = "error";
        return run;
    }
    if (process.exitStatus() == QProcess::NormalExit && run.exitCode == 0 && run.result["status"] == "ok") {
        run.kind = RunResult::Finished;
        return run;
    }
    const bool refusedExit = run.exitCode == 3 || run.exitCode == sandboxUnavailableExit;
    if (process.exitStatus() == QProcess::NormalExit && refusedExit &&
        run.result["status"] == "refused") {
        run.kind = RunResult::Refused;
        return run;
    }
    run.kind = RunResult::Stopped;
    // The seccomp filter kills a helper that makes a call its policy does not allow, with SIGSYS,
    // and QProcess reports the signal as the exit code of a crash (ADR 0183 section 4.6). It is not
    // a crash: a crash restarts nothing here, and a violation is a named reason of its own.
    if (process.exitStatus() == QProcess::CrashExit && process.exitCode() == SIGSYS)
        run.reason = "sandboxViolation";
    else if (process.exitStatus() == QProcess::CrashExit)
        run.reason = "crash";
    else if (run.logTail.contains("bad_alloc") || run.logTail.contains("std::length_error"))
        run.reason = "memory";
    else
        run.reason = "error";
    return run;
}

// Copies a regular file of at most maxFileBytes into `destination` (mode 0600). The source is opened without
// following a link and without blocking, and what is read is bounded: a file swapped for a pipe or a device,
// or grown, between the check and the copy cannot fill the disk.
void copyBounded(const QString &source, const QString &destination) {
    const int in = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (in < 0)
        refuse("unreadable", "The SketchUp file cannot be read.");
    struct stat info {};
    if (::fstat(in, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(in);
        refuse("notAFile", "The SketchUp source must be a regular file.");
    }
    const int out = ::open(QFile::encodeName(destination).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) {
        ::close(in);
        refuse("scratch", "The private copy of the SketchUp file could not be created.");
    }
    std::vector<char> buffer(1 << 20);
    qint64 total = 0;
    bool tooLarge = false, failed = false;
    for (;;) {
        const ssize_t got = ::read(in, buffer.data(), buffer.size());
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0) {
            failed = true;
            break;
        }
        if (got == 0)
            break;
        total += got;
        if (total > maxFileBytes) {
            tooLarge = true;
            break;
        }
        for (ssize_t done = 0; done < got;) {
            const ssize_t put = ::write(out, buffer.data() + done, std::size_t(got - done));
            if (put < 0 && errno == EINTR)
                continue;
            if (put <= 0) {
                failed = true;
                break;
            }
            done += put;
        }
        if (failed)
            break;
    }
    ::close(in);
    ::close(out);
    if (tooLarge)
        refuse("tooLarge", "The SketchUp file is larger than 256 MiB.");
    if (failed)
        refuse("unreadable", "The SketchUp file could not be copied into the private working directory.");
}

} // namespace

QString findSkpWorker(const QString &explicitPath) {
    if (!explicitPath.isEmpty())
        return QFileInfo(explicitPath).isExecutable() ? QFileInfo(explicitPath).absoluteFilePath() : QString();
    const QDir app(QCoreApplication::applicationDirPath());
    for (const auto &candidate : {app.absoluteFilePath("../lib/sketchyup/sketchyup-skp-worker"),
                                  app.absoluteFilePath("sketchyup-skp-worker")}) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.canonicalFilePath();
    }
    return {};
}

SkpImport importSkpFile(const QString &path, const SkpWorkerOptions &worker) {
    {
        // Cheap checks on the named file; the content checks run on the private copy below, so what is checked
        // is what is read even if the file changes in between.
        const QFileInfo info(path);
        if (!info.isFile())
            refuse("notAFile", "The SketchUp source must be a regular file.");
        if (info.size() > maxFileBytes)
            refuse("tooLarge", "The SketchUp file is larger than 256 MiB (" + std::to_string(info.size()) + " bytes).");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            refuse("unreadable", "The SketchUp file cannot be read.");
        const QByteArray head = file.read(4);
        // The SketchUp header: the UTF-16 byte order mark and the 0x0E of the marker (the helper checks it again).
        if (head.size() < 4 || static_cast<unsigned char>(head[0]) != 0xFF || static_cast<unsigned char>(head[1]) != 0xFE ||
            static_cast<unsigned char>(head[2]) != 0xFF || static_cast<unsigned char>(head[3]) != 0x0E)
            refuse("notSkp", "The file does not start with a SketchUp header; it is not a SketchUp (.skp) file.");
    }
    const QString workerPath = findSkpWorker(worker.workerPath);
    if (workerPath.isEmpty())
        refuse("workerMissing", "The SketchUp import runs in its helper (sketchyup-skp-worker), which is not installed "
                                "or cannot be run. Nothing else is affected.");
    const QString root = worker.scratchRoot.isEmpty() ? QDir::tempPath() : worker.scratchRoot;
    sweepStale(root);
    QTemporaryDir directory(QDir(root).filePath(QString(privatePrefix) + "XXXXXX"));
    if (!directory.isValid())
        refuse("scratch", "A private working directory could not be created.");
    QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // Copy the file in and hash the copy. The copy keeps the file's name: the package names the root
    // group after it.
    const QString input = QDir(directory.path()).filePath(QFileInfo(path).fileName());
    copyBounded(path, input);
    QFile::setPermissions(input, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    {
        QFile copy(input);
        if (!copy.open(QIODevice::ReadOnly) || copy.size() > maxFileBytes)
            refuse("unreadable", "The private copy of the SketchUp file cannot be read.");
        if (!hash.addData(&copy))
            refuse("unreadable", "The private copy of the SketchUp file cannot be hashed.");
    }
    const QString output = QDir(directory.path()).filePath("package.skskpp");
    const RunResult run = runOnce(workerPath, directory.path(), input, output, worker);
    const QJsonObject source{{"sha256", QString::fromLatin1(hash.result().toHex())},
                             {"bytes", QFileInfo(input).size()}, {"name", QFileInfo(input).fileName()}};
    const QJsonObject envelope{{"apiVersion", 1}, {"format", "SketchUp"}, {"direction", "import"}, {"source", source}};
    if (run.kind == RunResult::Cancelled)
        throw IfcImportError("cancelled", "The import was cancelled.");
    if (run.kind == RunResult::Stopped && run.reason == "sandboxViolation") {
        // ADR 0183 section 4.6: the message names the helper and the layer, never the call that was
        // refused.
        QJsonObject report = envelope;
        report["helper"] =
            QJsonObject{{"completed", false}, {"stop", run.reason}, {"phase", run.phase}};
        throw IfcImportError(
            "sandboxViolation",
            "The SketchUp helper (sketchyup-skp-worker) was stopped by its sandbox: the seccomp "
            "filter "
            "ended it for an action its policy does not allow. Nothing was imported.",
            report);
    }
    if (run.kind == RunResult::Stopped) {
        QJsonObject report = envelope;
        report["helper"] = QJsonObject{{"completed", false}, {"stop", run.reason}, {"phase", run.phase}, {"logTail", run.logTail}};
        throw IfcImportError("workerFailed", "The SketchUp helper stopped (" + run.reason.toStdString() + ").", report);
    }
    if (run.kind == RunResult::Refused) {
        // The helper's words are data: a code is a short identifier or it is "workerRefused", a message is bounded.
        static const QRegularExpression identifier("^[A-Za-z][A-Za-z0-9]{0,39}\\z");
        static const QRegularExpression control("[\\x00-\\x1f\\x7f]");
        QString code = run.result["code"].toString();
        if (!identifier.match(code).hasMatch())
            code = "workerRefused";
        QString message = clipped(run.result["message"].toString(), 240);
        message.replace(control, " ");
        while (message.toUtf8().size() > 240)
            message.chop(1);
        throw IfcImportError(code.toStdString(), message.toStdString(), envelope);
    }
    if (worker.progress)
        worker.progress("build", 0); // the application's own phases: the package is decoded, then converted
    QFile packageFile(output);
    if (!packageFile.open(QIODevice::ReadOnly) || packageFile.size() > maxPackageBytes)
        throw IfcImportError("package", "The SketchUp helper's package is missing or larger than its bound.");
    const auto bytes = packageFile.readAll();
    std::vector<std::uint8_t> raw(bytes.begin(), bytes.end());
    SkpPackage package;
    try {
        package = decodeSkpPackage(raw);
    } catch (const std::exception &error) {
        throw IfcImportError("package", error.what(), envelope);
    }
    if (worker.progress)
        worker.progress("create", 0);
    // The conversion checks the cancel between elements too, so a Cancel during the decode or the
    // conversion ends the import with nothing changed.
    SkpImport imported = importSkpPackage(package, SkpImportOptions{}, SkpConversionHooks{worker.cancelled});
    imported.report["helper"] = run.result;
    imported.report["run"] = QJsonObject{{"deadlineSeconds", worker.deadlineSeconds},
                                         {"addressSpaceBytes", worker.addressSpaceBytes}};
    return imported;
}

} // namespace sketchy
