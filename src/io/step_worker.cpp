#include "io/step_worker.hpp"
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
const char *const privatePrefix = "sketchyup-step-";

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
    int at{-1}; // the task the helper is about to start
    QString logTail;
    int exitCode{-1};
};

QString shortLog(QString text) {
    text = text.trimmed();
    if (text.size() > maxLogBytes)
        text = text.right(maxLogBytes);
    return text;
}

// The raw-text unit scan, the helper's own in the application's own bounded code (the mirror of
// src/ifc/step_probe.cpp's rawUnits for the length unit; the helper stays the authority, the
// scan is for the dialog's facts line): the first LENGTH_UNIT entity, its SI_UNIT (a prefixed
// metre, or a named base) or its conversion unit's quoted name, from the file as written.
// Whitespace outside single quotes is dropped (a writer may write LENGTH_UNIT ( )).
QString compactText(const QString &text) {
    QString out;
    out.reserve(text.size());
    bool quoted = false;
    for (const QChar c : text) {
        if (c == '\'') {
            quoted = !quoted;
            out.push_back(c);
            continue;
        }
        if (!quoted && c.isSpace())
            continue;
        out.push_back(c);
    }
    return out;
}
// The entity a marker sits in (the compacted text): from the previous ';' to the next ';' (the
// unit's own entity is read, not some other entity that merely mentions the same word).
QString entityAt(const QString &text, int pos) {
    int start = 0;
    if (const int before = text.rfind(';', pos); before >= 0)
        start = before + 1;
    int end = text.find(';', pos);
    if (end < 0)
        end = text.size();
    return text.mid(start, end - start);
}
// SI_UNIT( . prefix . base . ): the two arguments as dot literals (.MILLI. -> MILLI, $ -> "").
bool siUnitArgs(const QString &text, int from, QString &prefix, QString &base) {
    const int open = text.indexOf("SI_UNIT(", from);
    if (open < 0)
        return false;
    int pos = open + 8;
    const int comma = text.indexOf(',', pos);
    if (comma < 0 || comma - pos > 64)
        return false;
    auto dotLiteral = [](QString arg) {
        while (!arg.isEmpty() && arg.front() == '.')
            arg.remove(0, 1);
        while (!arg.isEmpty() && arg.back() == '.')
            arg.chop(1);
        return arg;
    };
    prefix = dotLiteral(text.mid(pos, comma - pos));
    const int close = text.indexOf(')', comma + 1);
    if (close < 0 || close - comma > 64)
        return false;
    base = dotLiteral(text.mid(comma + 1, close - comma - 1));
    return true;
}
// The unit an entity carries, as (prefix, base-or-name): an SI_UNIT wins (an SI unit's NAMED_UNIT
// carries the "*" name and stands before the SI_UNIT in the same entity); otherwise a
// CONVERSION_BASED_UNIT, whose name is a quoted string; a bare NAMED_UNIT has no unit to read.
std::optional<std::pair<QString, QString>> unitIn(const QString &entity) {
    if (const int si = entity.indexOf("SI_UNIT("); si >= 0) {
        QString prefix, base;
        if (siUnitArgs(entity, si, prefix, base))
            return std::make_pair(prefix, base);
    }
    if (const int conv = entity.indexOf("CONVERSION_BASED_UNIT("); conv >= 0) {
        const int open = entity.indexOf('\'', conv + 22);
        if (open < 0 || open - conv > 64)
            return std::nullopt;
        const int close = entity.indexOf('\'', open + 1);
        if (close < 0 || close - open > 64)
            return std::nullopt;
        return std::make_pair(QString(), entity.mid(open + 1, close - open - 1));
    }
    return std::nullopt;
}
// The declared length unit's name ("" when the raw text declares none): the SI metre with its
// prefix (millimetre, ...), or the conversion unit's quoted name (INCH, FOOT, ...), as the
// helper's scan spells it.
QString declaredLengthUnit(const QString &text) {
    static const QHash<QString, QString> prefix = {QStringLiteral("MILLI"), QStringLiteral("millimetre"),
                                                  {QStringLiteral("CENTI"), QStringLiteral("centimetre")},
                                                  {QStringLiteral("DECI"), QStringLiteral("decimetre")},
                                                  {QStringLiteral("KILO"), QStringLiteral("kilometre")},
                                                  {QString(), QStringLiteral("metre")}};
    static const QHash<QString, QString> named = {QStringLiteral("INCH"), QStringLiteral("INCH"),
                                                 {QStringLiteral("FOOT"), QStringLiteral("FOOT")},
                                                 {QStringLiteral("MILLIMETRE"), QStringLiteral("MILLIMETRE")},
                                                 {QStringLiteral("CENTIMETRE"), QStringLiteral("CENTIMETRE")},
                                                 {QStringLiteral("METRE"), QStringLiteral("METRE")}};
    const int at = text.indexOf("LENGTH_UNIT(");
    if (at < 0)
        return {};
    const QString entity = entityAt(text, at);
    const auto unit = unitIn(entity);
    if (!unit)
        return {};
    if (unit->second == "METRE")
        return prefix.value(unit->first, unit->first); // declared but a prefix the table does not know
    const QString folded = unit->second.toLower();
    if (const auto found = named.constFind(folded); found != named.cend())
        return *found;
    return unit->second; // declared, a name the table does not know
}
} // namespace

StepScan checkStepSource(const QString &path) {
    // The cheap checks of the application's own bounded code, in the helper's own terms: the
    // ISO-10303-21 header the reader requires (the helper's scan is a strict prefix, no BOM),
    // the FILE_SCHEMA the helper accepts (AP203, AP214, AP242), and the declared unit (display;
    // the helper's scan is the authority).
    const QFileInfo info(path);
    if (!info.isFile())
        refuse("notAFile", "The STEP source must be a regular file.");
    if (info.size() > maxFileBytes)
        refuse("tooLarge", "The STEP file is larger than 128 MiB (" + std::to_string(info.size()) + " bytes).");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        refuse("unreadable", "The STEP file cannot be read.");
    const auto head = file.read(256 * 1024);
    if (head.startsWith("PK\x03\x04"))
        refuse("unsupportedFormat", "A compressed file is not imported; extract the STEP file inside it.");
    const auto trimmed = head.trimmed();
    if (trimmed.startsWith("<?xml") || trimmed.startsWith("<step") || trimmed.startsWith("{") || trimmed.startsWith("["))
        refuse("unsupportedFormat", "A STEP-XML or JSON file is not imported; only STEP physical files (.step) are.");
    if (!head.startsWith("ISO-10303-21"))
        refuse("notStep", "The file does not start with ISO-10303-21; it is not a STEP file.");
    // FILE_SCHEMA: the first quoted name after the identifier, as the helper's scan reads it.
    const int schemaAt = head.indexOf("FILE_SCHEMA(");
    if (schemaAt < 0)
        refuse("noSchema", "The file declares no FILE_SCHEMA in its header.");
    const int open = head.indexOf('\'', schemaAt);
    const int close = open < 0 ? -1 : head.indexOf('\'', open + 1);
    if (close < 0 || close - open > 256)
        refuse("noSchema", "The file's FILE_SCHEMA has no readable name.");
    const QString raw = head.mid(open + 1, close - open - 1);
    // The schema identifier's part (the digits after 10303): 203, 214 or 442, as the helper classifies it.
    const int at = raw.indexOf("10303");
    QString schema;
    if (at >= 0) {
        int pos = at + 5;
        while (pos < raw.size() && !QChar::isDigit(raw.at(pos)))
            ++pos;
        const int digits = pos;
        while (pos < raw.size() && QChar::isDigit(raw.at(pos)))
            ++pos;
        if (pos > digits && pos - digits <= 5) {
            const QString part = raw.mid(digits, pos - digits);
            if (part == "203")
                schema = "ap203";
            else if (part == "214")
                schema = "ap214";
            else if (part == "442")
                schema = "ap242";
        }
    }
    if (schema.isEmpty())
        refuse("unsupportedSchema",
                "The schema " + clipped(raw, 40).toStdString() + " is not AP203, AP214 or AP242; it is refused.");
    StepScan scan;
    scan.schema = schema;
    // The declared unit is display only (the facts line): the helper's scan is the authority and
    // the factor is the helper's. A unit declared but the reader could not resolve is the
    // helper's refusal (ADR 0172 section 2), not the application's.
    scan.unit = declaredLengthUnit(compactText(QString::fromLatin1(head)));
    scan.unitDeclared = !scan.unit.isEmpty();
    return scan;
}

namespace {
RunResult runOnce(const QString &worker, const QString &directory, const QString &input, const QString &output,
                  const StepWorkerOptions &options) {
    RunResult run;
    QStringList args{"--import-step", input, "--output", output,
                     "--step-deflection-mm", QString::number(options.deflectionMm, 'g', 12),
                     "--step-angle-deg", QString::number(options.angleDeg, 'g', 12),
                     "--step-max-triangles", QString::number(options.maxTrianglesPerElement),
                     "--step-max-total-triangles", QString::number(options.maxTotalTriangles)};
    if (!options.lengthUnitOverride.isEmpty())
        args << "--step-length-unit" << options.lengthUnitOverride;
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
            } else if (line.startsWith("P tasks ")) {
                if (options.progress)
                    options.progress("tasks", line.mid(8).toLongLong());
            } else if (line.startsWith("P at ")) {
                bool ok{};
                const int task = line.mid(5).toInt(&ok);
                run.at = ok ? task : -1;
                if (ok && options.progress)
                    options.progress("at", task);
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

// Copies a regular file of at most maxFileBytes into `destination` (mode 0600). The source is opened without
// following a link and without blocking, and what is read is bounded: a file that is swapped for a pipe or a device, or
// grows, between the check and the copy cannot fill the disk.
void copyBounded(const QString &source, const QString &destination) {
    const int in = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (in < 0)
        refuse("unreadable", "The STEP file cannot be read.");
    struct stat info {};
    if (::fstat(in, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(in);
        refuse("notAFile", "The STEP source must be a regular file.");
    }
    const int out = ::open(QFile::encodeName(destination).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) {
        ::close(in);
        refuse("scratch", "The private copy of the STEP file could not be created.");
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
        refuse("tooLarge", "The STEP file is larger than 128 MiB.");
    if (failed)
        refuse("unreadable", "The STEP file could not be copied into the private working directory.");
}
} // namespace

IfcImport importStepFile(const QString &path, const StepWorkerOptions &worker) {
    {
        // Cheap checks on the named file; the content checks run on the private copy below, so what is checked is
        // what is read even if the file changes in between.
        const QFileInfo info(path);
        if (!info.isFile())
            refuse("notAFile", "The STEP source must be a regular file.");
        if (info.size() > maxFileBytes)
            refuse("tooLarge", "The STEP file is larger than 128 MiB (" + std::to_string(info.size()) + " bytes).");
    }
    // The STEP import runs in the IFC helper (ADR 0172 section 8: one component, one helper, two directions).
    const QString workerPath = findIfcWorker(worker.workerPath);
    if (workerPath.isEmpty())
        refuse("workerMissing",
               "The STEP import runs in the IFC helper (sketchyup-ifc-worker), which is not installed or cannot be "
               "run; install the sketchyup-ifc package. Nothing else is affected.");
    const QString root = worker.scratchRoot.isEmpty() ? QDir::tempPath() : worker.scratchRoot;
    sweepStale(root);
    QTemporaryDir directory(QDir(root).filePath(QString(privatePrefix) + "XXXXXX"));
    if (!directory.isValid())
        refuse("scratch", "A private working directory could not be created.");
    QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // Copy the file in and hash the copy: what is checked is what is read.
    const QString input = QDir(directory.path()).filePath("input.step");
    copyBounded(path, input);
    QFile::setPermissions(input, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const StepScan scan = checkStepSource(input);
    const QString schema = scan.schema;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    {
        QFile copy(input);
        if (!copy.open(QIODevice::ReadOnly) || copy.size() > maxFileBytes)
            refuse("unreadable", "The private copy of the STEP file cannot be read.");
        if (!hash.addData(&copy))
            refuse("unreadable", "The private copy of the STEP file cannot be hashed.");
    }
    const QString output = QDir(directory.path()).filePath("package.skifc");
    const RunResult run = runOnce(workerPath, directory.path(), input, output, worker);
    if (run.kind == RunResult::Cancelled)
        throw IfcImportError("cancelled", "The import was cancelled.");
    if (run.kind != RunResult::Finished && run.kind != RunResult::Refused) {
        // The STEP import has no per-element restart protocol: a stop is a failure, reported with what the helper
        // left behind.
        QJsonObject report{{"apiVersion", 1}, {"format", "STEP"}, {"direction", "import"},
                           {"engine", QJsonObject{{"completed", false}, {"restarts", 0},
                                                  {"stops", QJsonArray::fromStringList(QStringList{run.reason + " (" + run.phase + ")"})}},
                           {"logTail", run.logTail}};
        throw IfcImportError("workerFailed", "The STEP helper stopped (" + run.reason.toStdString() + ").", report);
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
    facts.run = QJsonObject{{"restarts", 0},
                            {"deadlineSeconds", worker.deadlineSeconds},
                            {"addressSpaceBytes", worker.addressSpaceBytes},
                            {"completed", true}};
    // The package-to-document conversion is the IFC one as-is (ADR 0172 section 8): the package's nodes,
    // meshes, units and report are its data. Spaces and georeference have no STEP meaning and keep their
    // defaults.
    IfcConversionHooks hooks;
    hooks.cancelled = worker.cancelled;
    auto imported = importIfcPackage(package, IfcImportOptions{}, facts, hooks);
    // The application's own words for the report the conversion made for a STEP package: the format is
    // STEP, the tessellation policy is the application's own choice, the helper's own line rides the
    // report as `step`, and the accounting identity is computed from the package it read (the helper's
    // package holds the converted nodes and the root; the conversion omits nothing — the helper refuses
    // before it writes anything it cannot convert).
    auto &report = imported.report;
    report["format"] = "STEP";
    report["tessellation"] = QJsonObject{{"deflectionMm", worker.deflectionMm},
                                           {"angleDeg", worker.angleDeg},
                                           {"maxTrianglesPerElement", worker.maxTrianglesPerElement},
                                           {"maxTotalTriangles", worker.maxTotalTriangles}};
    report["step"] = run.result;
    const qint64 nodes = qint64(package.nodes.size());
    const qint64 converted = report["geometry"].toObject()["bodies"].toInteger(0);
    report["accounting"] = QJsonObject{{"entities", nodes},
                                       {"converted", converted},
                                       {"supporting", nodes - converted},
                                       {"omitted", 0LL}};
    return imported;
}
} // namespace sketchy
