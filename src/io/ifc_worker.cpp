#include "io/ifc_worker.hpp"

#include <algorithm>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>
namespace sketchy {
namespace {
constexpr qint64 maxFileBytes = 128 * 1024 * 1024;
constexpr qint64 maxPackageBytes = 24 + qint64(IfcPackageLimits::manifestBytes) + qint64(IfcPackageLimits::meshBytes);
constexpr qint64 maxLogBytes = 2048;
const char *const privatePrefix = "sketchyup-ifc-";

[[noreturn]] void refuse(const std::string &code, const std::string &message) { throw IfcImportError(code, message); }
QString clipped(QString text, int characters) { return text.size() > characters ? text.left(characters) : text; }

// Removes private directories an earlier run left behind (the application was killed): older than a day.
void sweepStale(const QString &root) {
    // Real directories of ours only: never a symbolic link (in a shared temporary directory another user could
    // plant one that points at something of ours) and never one that somebody else owns.
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
    QString reason;     // for Stopped: crash, timeout, memory, error
    QJsonObject result; // the "R" line
    QString phase;
    QList<int> next;
    int at{-1};              // the task the helper was about to start (single thread)
    QList<int> foundExcluded; // products the helper itself found to stop it and left out
    int threads{};
    QString logTail;
    int exitCode{-1};
};

QString shortLog(QString text) {
    text = text.trimmed();
    if (text.size() > maxLogBytes)
        text = text.right(maxLogBytes);
    return text;
}
} // namespace

QString findIfcWorker(const QString &explicitPath) {
    if (!explicitPath.isEmpty())
        return QFileInfo(explicitPath).isExecutable() ? QFileInfo(explicitPath).absoluteFilePath() : QString();
    const QDir app(QCoreApplication::applicationDirPath());
    for (const auto &candidate : {app.absoluteFilePath("../lib/sketchyup/sketchyup-ifc-worker"),
                                  app.absoluteFilePath("sketchyup-ifc-worker")}) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.canonicalFilePath();
    }
    return {};
}

QString checkIfcSource(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile())
        refuse("notAFile", "The IFC source must be a regular file.");
    if (info.size() > maxFileBytes)
        refuse("tooLarge", "The IFC file is larger than 128 MiB (" + std::to_string(info.size()) + " bytes).");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        refuse("unreadable", "The IFC file cannot be read.");
    const auto head = file.read(256 * 1024);
    auto trimmedHead = head;
    if (trimmedHead.startsWith("\xEF\xBB\xBF"))
        trimmedHead = trimmedHead.mid(3);
    if (trimmedHead.startsWith("PK\x03\x04"))
        refuse("unsupportedFormat", "A compressed .ifczip file is not imported; extract the .ifc inside it.");
    if (trimmedHead.trimmed().startsWith("<?xml") || trimmedHead.trimmed().startsWith("<ifc"))
        refuse("unsupportedFormat", "An ifcXML file is not imported; only STEP physical files (.ifc) are.");
    if (trimmedHead.trimmed().startsWith("{") || trimmedHead.trimmed().startsWith("["))
        refuse("unsupportedFormat", "An ifcJSON file is not imported; only STEP physical files (.ifc) are.");
    if (!trimmedHead.trimmed().startsWith("ISO-10303-21;"))
        refuse("notIfc", "The file does not start with ISO-10303-21; it is not an IFC STEP file.");
    file.seek(std::max<qint64>(0, info.size() - 4096));
    const auto tail = file.readAll().trimmed();
    if (!tail.endsWith("END-ISO-10303-21;"))
        refuse("truncated", "The file does not end with END-ISO-10303-21; (it may be truncated).");
    // FILE_SCHEMA: exactly one accepted identifier.
    static const QRegularExpression schemaPattern(
        R"(FILE_SCHEMA\s*\(\s*\(([^)]*)\)\s*\))", QRegularExpression::CaseInsensitiveOption);
    const auto match = schemaPattern.match(QString::fromLatin1(head));
    if (!match.hasMatch())
        refuse("noSchema", "The file declares no FILE_SCHEMA in its header.");
    QStringList names;
    for (const auto &part : match.captured(1).split(',', Qt::SkipEmptyParts)) {
        auto name = part.trimmed();
        if (name.startsWith('\'') && name.endsWith('\'') && name.size() >= 2)
            name = name.mid(1, name.size() - 2);
        names.append(name.toUpper());
    }
    if (names.size() != 1)
        refuse("unsupportedSchema", "A file that declares several schemas is not imported.");
    const auto schema = names.front();
    if (schema != "IFC2X3" && schema != "IFC4" && schema != "IFC4X3_ADD2")
        refuse("unsupportedSchema", "The schema " + clipped(schema, 40).toStdString() +
                                        " is not imported; IFC2X3, IFC4 and IFC4X3_ADD2 are.");
    return schema;
}

namespace {
RunResult runOnce(const QString &worker, const QString &directory, const QString &input, const QString &output,
                  const IfcWorkerOptions &options, const IfcImportOptions &importOptions, int threads,
                  const QList<int> &excluded, int diagnoseFrom) {
    RunResult run;
    QStringList args{"--import", input, "--output", output, "--threads", QString::number(threads), "--deflection",
                     QString::number(options.chordDeviationMm / 1000.0, 'g', 12), "--angular-deflection", "0.5",
                     "--max-voids", QString::number(options.maxVoidsPerElement), "--max-entities",
                     QString::number(options.maxEntities)};
    if (!options.lengthUnit.isEmpty())
        args << "--length-unit" << options.lengthUnit;
    if (importOptions.spaces == IfcSpaces::Skip)
        args << "--skip-spaces";
    if (importOptions.georeference == IfcGeoreference::Grid)
        args << "--georef" << "grid";
    for (const int step : excluded)
        args << "--exclude-step" << QString::number(step);
    if (diagnoseFrom >= 0)
        args << "--diagnose-from" << QString::number(diagnoseFrom) << "--diagnose-seconds"
             << QString::number(std::clamp(options.deadlineSeconds / 8, 1, 20));
    if (options.addressSpaceBytes > 0)
        args << "--rlimit-as" << QString::number(options.addressSpaceBytes);
    args << "--rlimit-cpu" << QString::number(qint64(options.deadlineSeconds) * 4) << "--rlimit-fsize"
         << QString::number(qint64(256) * 1024 * 1024);
    if (!options.testFault.isEmpty())
        args << "--test-fault" << options.testFault;
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
            } else if (line.startsWith("P next")) {
                run.next.clear();
                for (const auto &number : line.mid(6).split(' ', Qt::SkipEmptyParts)) {
                    bool ok{};
                    const int step = number.toInt(&ok);
                    if (ok && run.next.size() < 1024)
                        run.next.append(step);
                }
            } else if (line.startsWith("P at ")) {
                bool ok{};
                const int task = line.mid(5).toInt(&ok);
                run.at = ok ? task : -1;
            } else if (line.startsWith("P excluded")) {
                for (const auto &number : line.mid(10).split(' ', Qt::SkipEmptyParts)) {
                    bool ok{};
                    const int step = number.toInt(&ok);
                    if (ok && run.foundExcluded.size() < 1024)
                        run.foundExcluded.append(step);
                }
            } else if (line.startsWith("P threads ")) {
                run.threads = line.mid(10).toInt();
            } else if (line.startsWith("P tasks ")) {
                if (options.progress)
                    options.progress("tasks", line.mid(8).toLongLong());
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
        run.next.clear();
        return run;
    }
    if (process.exitStatus() == QProcess::NormalExit && run.exitCode == 0 && run.result["status"] == "ok") {
        run.kind = RunResult::Finished;
        return run;
    }
    if (process.exitStatus() == QProcess::NormalExit && run.exitCode == 3 && run.result["status"] == "refused") {
        run.kind = RunResult::Refused;
        return run;
    }
    run.kind = RunResult::Stopped;
    if (process.exitStatus() == QProcess::CrashExit)
        run.reason = "crash";
    else if (run.logTail.contains("bad_alloc") || run.logTail.contains("std::length_error"))
        run.reason = "memory";
    else
        run.reason = "error";
    return run;
}
} // namespace

namespace {
// Copies a regular file of at most maxFileBytes into `destination` (mode 0600). The source is opened without
// following a link and without blocking, and what is read is bounded: a file that is swapped for a pipe or a device, or
// grows, between the check and the copy cannot fill the disk.
void copyBounded(const QString &source, const QString &destination) {
    const int in = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (in < 0)
        refuse("unreadable", "The IFC file cannot be read.");
    struct stat info {};
    if (::fstat(in, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(in);
        refuse("notAFile", "The IFC source must be a regular file.");
    }
    const int out = ::open(QFile::encodeName(destination).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) {
        ::close(in);
        refuse("scratch", "The private copy of the IFC file could not be created.");
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
        refuse("tooLarge", "The IFC file is larger than 128 MiB.");
    if (failed)
        refuse("unreadable", "The IFC file could not be copied into the private working directory.");
}
} // namespace

IfcImport importIfcFile(const QString &path, const IfcImportOptions &options, const IfcWorkerOptions &worker) {
    {
        // Cheap checks on the named file; the content checks run on the private copy below, so what is checked is
        // what is read even if the file changes in between.
        const QFileInfo info(path);
        if (!info.isFile())
            refuse("notAFile", "The IFC source must be a regular file.");
        if (info.size() > maxFileBytes)
            refuse("tooLarge", "The IFC file is larger than 128 MiB (" + std::to_string(info.size()) + " bytes).");
    }
    const QString workerPath = findIfcWorker(worker.workerPath);
    if (workerPath.isEmpty())
        refuse("workerMissing",
               "The IFC helper (sketchyup-ifc-worker) is not installed or cannot be run; install the sketchyup-ifc "
               "package. Nothing else is affected.");
    const QString root = worker.scratchRoot.isEmpty() ? QDir::tempPath() : worker.scratchRoot;
    sweepStale(root);
    QTemporaryDir directory(QDir(root).filePath(QString(privatePrefix) + "XXXXXX"));
    if (!directory.isValid())
        refuse("scratch", "A private working directory could not be created.");
    QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // Copy the file in and hash the copy: what is checked is what is read.
    const QString input = QDir(directory.path()).filePath("input.ifc");
    copyBounded(path, input);
    QFile::setPermissions(input, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QString schema = checkIfcSource(input);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    {
        QFile copy(input);
        if (!copy.open(QIODevice::ReadOnly) || copy.size() > maxFileBytes)
            refuse("unreadable", "The private copy of the IFC file cannot be read.");
        if (!hash.addData(&copy))
            refuse("unreadable", "The private copy of the IFC file cannot be hashed.");
    }
    const QString output = QDir(directory.path()).filePath("package.skifc");

    QList<int> excluded;
    QJsonArray excludedReport;
    QStringList stops;
    int threads = std::max(1, std::min(worker.threads, 4));
    RunResult run;
    int restarts = 0;
    int diagnoseFrom = -1;   // >= 0: the helper tries the tasks from there on in child processes first
    QString lastReason = "crash";
    for (;; ++restarts) {
        QFile::remove(output);
        const bool diagnosing = diagnoseFrom >= 0;
        run = runOnce(workerPath, directory.path(), input, output, worker, options, threads, excluded, diagnoseFrom);
        // What the helper found to stop it and left out itself is recorded whatever happens next.
        for (const int step : run.foundExcluded) {
            if (excluded.contains(step))
                continue;
            excluded.append(step);
            excludedReport.append(QJsonObject{{"step", step}, {"reason", lastReason}});
        }
        if (run.kind == RunResult::Finished || run.kind == RunResult::Refused)
            break;
        if (run.kind == RunResult::Cancelled)
            throw IfcImportError("cancelled", "The import was cancelled.");
        stops.append(run.reason + " (" + run.phase + ")");
        lastReason = run.reason;
        QJsonObject report{{"apiVersion", 1}, {"format", "IFC"}, {"direction", "import"},
                           {"engine", QJsonObject{{"completed", false}, {"restarts", restarts}, {"stops", QJsonArray::fromStringList(stops)}}},
                           {"excluded", excludedReport},
                           {"logTail", run.logTail}};
        if (restarts >= worker.maxRestarts)
            throw IfcImportError("workerFailed",
                                 "The IFC helper stopped (" + stops.last().toStdString() + ") and the import failed after " +
                                     std::to_string(restarts) + " restart(s).",
                                 report);
        if (threads > 1 && run.phase == "geometry") {
            // Several threads were converting: which element stopped it is unknown, so run again on one thread.
            threads = 1;
            continue;
        }
        if (run.phase == "geometry" && run.at >= 0 && !(diagnosing && run.foundExcluded.isEmpty())) {
            // On one thread the task it stopped on is known only up to the tasks that yield nothing (the iterator
            // skips them): the next run tries the tasks from there one at a time in child processes.
            diagnoseFrom = run.at;
            threads = 1;
            continue;
        }
        diagnoseFrom = -1;
        if (run.phase != "geometry" || run.next.isEmpty())
            throw IfcImportError("workerFailed",
                                 "The IFC helper stopped (" + stops.last().toStdString() +
                                     ") outside the geometry conversion, so no element can be left out.",
                                 report);
        for (const int step : run.next) {
            if (excluded.contains(step))
                continue;
            excluded.append(step);
            excludedReport.append(QJsonObject{{"step", step}, {"reason", run.reason}});
        }
        threads = 1;
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
        throw IfcImportError(code.toStdString(), message.toStdString(),
                             QJsonObject{{"apiVersion", 1}, {"format", "IFC"}, {"direction", "import"},
                                         {"source", QJsonObject{{"sha256", QString::fromLatin1(hash.result().toHex())},
                                                               {"bytes", QFileInfo(input).size()}, {"schema", schema}}}});
    }
    QFile packageFile(output);
    if (!packageFile.open(QIODevice::ReadOnly) || packageFile.size() > maxPackageBytes)
        throw IfcImportError("package", "The IFC helper's package is missing or larger than its bound.");
    const auto bytes = packageFile.readAll();
    IfcPackage package;
    try {
        package = decodeIfcPackage(bytes);
    } catch (const std::exception &error) {
        throw IfcImportError("package", error.what());
    }
    IfcRunFacts facts;
    facts.sha256 = QString::fromLatin1(hash.result().toHex());
    facts.bytes = QFileInfo(input).size();
    facts.run = QJsonObject{{"restarts", restarts},
                            {"threadsRequested", worker.threads},
                            {"deadlineSeconds", worker.deadlineSeconds},
                            {"addressSpaceBytes", worker.addressSpaceBytes},
                            {"completed", true}};
    IfcConversionHooks hooks;
    hooks.cancelled = worker.cancelled;
    auto result = importIfcPackage(package, options, facts, hooks);
    if (restarts > 0 || !excludedReport.isEmpty()) {
        auto report = result.report;
        report["excluded"] = excludedReport;
        auto notices = report["notices"].toArray();
        notices.append(QJsonObject{{"code", "restarted"},
                                   {"count", restarts},
                                   {"message", "The helper stopped on an element, was restarted, and the element was left out."}});
        report["notices"] = notices;
        boundIfcReport(report);
        result.report = report;
    }
    return result;
}

// R098.b (ADR 0172): the application's side of the STEP import. The cheap checks run on the named
// file, the content checks on the private copy, the helper runs with --import-step (the same
// environment scrub, resource limits and deadline as the IFC import), and the conversion is the
// same Converter (the tag family STEP, the label Import STEP).
namespace {
// The cheap checks of a STEP source (ADR 0172 section 5): regular file, at most 128 MiB, the
// ISO-10303-21 header, and exactly one accepted FILE_SCHEMA (AP203, AP214, AP242).
QString stepSourceSchema(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile())
        refuse("notAFile", "The STEP source must be a regular file.");
    if (info.size() > maxFileBytes)
        refuse("tooLarge", "The STEP file is larger than 128 MiB (" + std::to_string(info.size()) + " bytes).");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        refuse("unreadable", "The STEP file cannot be read.");
    const auto head = file.read(256 * 1024);
    auto trimmedHead = head;
    if (trimmedHead.startsWith("\xEF\xBB\xBF"))
        trimmedHead = trimmedHead.mid(3);
    if (trimmedHead.startsWith("PK\x03\x04"))
        refuse("unsupportedFormat", "A compressed file is not imported; extract the STEP file inside it.");
    if (!trimmedHead.trimmed().startsWith("ISO-10303-21;"))
        refuse("notStep", "The file does not start with ISO-10303-21; it is not a STEP file.");
    static const QRegularExpression schemaPattern(
        R"(FILE_SCHEMA\s*\(\s*\(([^)]*)\)\s*\))", QRegularExpression::CaseInsensitiveOption);
    const auto match = schemaPattern.match(QString::fromLatin1(head));
    if (!match.hasMatch())
        refuse("noSchema", "The file declares no FILE_SCHEMA in its header.");
    QStringList names;
    for (const auto &part : match.captured(1).split(',', Qt::SkipEmptyParts)) {
        auto name = part.trimmed();
        if (name.startsWith('\'') && name.endsWith('\'') && name.size() >= 2)
            name = name.mid(1, name.size() - 2);
        names.append(name.toUpper());
    }
    if (names.size() != 1)
        refuse("unsupportedSchema", "A file that declares several schemas is not imported.");
    // The schema's name carries its version (AUTOMOTIVE_DESIGN { 1 0 10303 214 ... }): the worker's
    // own check classifies by the 10303-XXX number, and this one does the same.
    const int at = names.front().indexOf("10303");
    QString kind;
    if (at >= 0) {
        int digits = at + 5;
        while (digits < names.front().size() && !names.front()[digits].isDigit())
            ++digits;
        int end = digits;
        while (end < names.front().size() && names.front()[end].isDigit())
            ++end;
        if (digits != end && end - digits <= 5) {
            const auto part = names.front().mid(digits, end - digits);
            if (part == "203")
                kind = "AP203";
            else if (part == "214")
                kind = "AP214";
            else if (part == "442")
                kind = "AP242";
        }
    }
    if (kind.isEmpty())
        refuse("unsupportedSchema", "The schema " + clipped(names.front(), 40).toStdString() +
                                       " is not imported; AP203, AP214 and AP242 are.");
    return kind;
}
RunResult runStepOnce(const QString &worker, const QString &directory, const QString &input, const QString &output,
                      const IfcWorkerOptions &options) {
    RunResult run;
    QStringList args{"--import-step", input, "--output", output, "--step-deflection-mm",
                     QString::number(options.chordDeviationMm, 'g', 12)};
    if (!options.lengthUnit.isEmpty())
        args << "--step-length-unit" << options.lengthUnit;
    if (options.addressSpaceBytes > 0)
        args << "--rlimit-as" << QString::number(options.addressSpaceBytes);
    args << "--rlimit-cpu" << QString::number(qint64(options.deadlineSeconds) * 4) << "--rlimit-fsize"
         << QString::number(qint64(256) * 1024 * 1024);
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
                pending.clear();
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
    // The helper's refusal: exit 3 with the package's --output, exit 2 without it (the metadata path).
    if (process.exitStatus() == QProcess::NormalExit && (run.exitCode == 2 || run.exitCode == 3) && run.result["status"] == "refused") {
        run.kind = RunResult::Refused;
        return run;
    }
    run.kind = RunResult::Stopped;
    if (process.exitStatus() == QProcess::CrashExit)
        run.reason = "crash";
    else if (run.logTail.contains("bad_alloc") || run.logTail.contains("std::length_error"))
        run.reason = "memory";
    else
        run.reason = "error";
    return run;
}
} // namespace
IfcImport importStepFile(const QString &path, const IfcImportOptions &options, const IfcWorkerOptions &worker) {
    { // the cheap checks; the content checks run on the private copy below
        const QFileInfo info(path);
        if (!info.isFile())
            refuse("notAFile", "The STEP source must be a regular file.");
        if (info.size() > maxFileBytes)
            refuse("tooLarge", "The STEP file is larger than 128 MiB (" + std::to_string(info.size()) + " bytes).");
    }
    const QString workerPath = findIfcWorker(worker.workerPath);
    if (workerPath.isEmpty())
        refuse("workerMissing",
               "The exchange helper (sketchyup-ifc-worker) is not installed or cannot be run; install the "
               "sketchyup-ifc package. Nothing else is affected.");
    const QString root = worker.scratchRoot.isEmpty() ? QDir::tempPath() : worker.scratchRoot;
    sweepStale(root);
    QTemporaryDir directory(QDir(root).filePath(QString(privatePrefix) + "step-XXXXXX"));
    if (!directory.isValid())
        refuse("scratch", "A private working directory could not be created.");
    QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString input = QDir(directory.path()).filePath("input.step");
    copyBounded(path, input);
    QFile::setPermissions(input, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QString schema = stepSourceSchema(input);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    {
        QFile copy(input);
        if (!copy.open(QIODevice::ReadOnly) || copy.size() > maxFileBytes)
            refuse("unreadable", "The private copy of the STEP file cannot be read.");
        if (!hash.addData(&copy))
            refuse("unreadable", "The private copy of the STEP file cannot be hashed.");
    }
    const QString output = QDir(directory.path()).filePath("package.skifc");
    // The STEP conversion is one kernel transfer, so there is no element to leave out after a stop: a
    // stopped run is retried (on the file, not per element) and a stop that persists fails the import.
    QStringList stops;
    int restarts = 0;
    RunResult run;
    for (;; ++restarts) {
        QFile::remove(output);
        run = runStepOnce(workerPath, directory.path(), input, output, worker);
        if (run.kind == RunResult::Finished || run.kind == RunResult::Refused)
            break;
        if (run.kind == RunResult::Cancelled)
            throw IfcImportError("cancelled", "The import was cancelled.");
        stops.append(run.reason + " (" + run.phase + ")");
        if (restarts >= worker.maxRestarts)
            throw IfcImportError("workerFailed",
                                 "The STEP helper stopped (" + stops.last().toStdString() + ") and the import failed after " +
                                     std::to_string(restarts) + " restart(s).",
                                 QJsonObject{{"apiVersion", 1}, {"format", "STEP"}, {"direction", "import"},
                                             {"engine", QJsonObject{{"completed", false}, {"restarts", restarts},
                                                                     {"stops", QJsonArray::fromStringList(stops)}}},
                                             {"logTail", run.logTail}});
    }
    if (run.kind == RunResult::Refused) {
        static const QRegularExpression identifier("^[A-Za-z][A-Za-z0-9]{0,39}\\z");
        static const QRegularExpression control("[\\x00-\\x1f\\x7f]");
        QString code = run.result["code"].toString();
        if (!identifier.match(code).hasMatch())
            code = "workerRefused";
        QString message = clipped(run.result["message"].toString(), 240);
        message.replace(control, " ");
        while (message.toUtf8().size() > 240)
            message.chop(1);
        throw IfcImportError(code.toStdString(), message.toStdString(),
                             QJsonObject{{"apiVersion", 1}, {"format", "STEP"}, {"direction", "import"},
                                         {"source", QJsonObject{{"sha256", QString::fromLatin1(hash.result().toHex())},
                                                               {"bytes", QFileInfo(input).size()}, {"schema", schema}}}});
    }
    QFile packageFile(output);
    if (!packageFile.open(QIODevice::ReadOnly) || packageFile.size() > maxPackageBytes)
        throw IfcImportError("package", "The STEP helper's package is missing or larger than its bound.");
    const auto bytes = packageFile.readAll();
    IfcPackage package;
    try {
        package = decodeIfcPackage(bytes);
    } catch (const std::exception &error) {
        throw IfcImportError("package", error.what());
    }
    IfcRunFacts facts;
    facts.sha256 = QString::fromLatin1(hash.result().toHex());
    facts.bytes = QFileInfo(input).size();
    facts.run = QJsonObject{{"restarts", restarts},
                            {"threadsRequested", 1},
                            {"deadlineSeconds", worker.deadlineSeconds},
                            {"addressSpaceBytes", worker.addressSpaceBytes},
                            {"completed", true}};
    IfcConversionHooks hooks;
    hooks.cancelled = worker.cancelled;
    auto result = importIfcPackage(package, options, facts, hooks);
    if (restarts > 0) {
        auto report = result.report;
        auto notices = report["notices"].toArray();
        notices.append(QJsonObject{{"code", "restarted"},
                                   {"count", restarts},
                                   {"message", "The helper stopped, was restarted, and the import ran again."}});
        report["notices"] = notices;
        boundIfcReport(report);
        result.report = report;
    }
    return result;
}
} // namespace sketchy
