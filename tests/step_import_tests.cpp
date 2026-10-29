// R098.a3 (ADR 0172 section 8): the application side of the STEP import -- the application's own
// cheap checks (the same shape as the helper's), the run of the helper's --import-step, the
// progress protocol, the package, and the application's own CLI.
//
// The heavy work is never done here: a stand-in helper (a shell script) answers --import-step,
// plays the progress protocol and writes a conversion package. The stand-in can be given a real
// package (one the real helper wrote, when the fixture is present) or it writes one the test built
// (encodeIfcPackage), and it can refuse or hang.

#include "io/ifc_import.hpp"
#include "io/step_worker.hpp"
#include "ifc_package_fixtures.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <sys/stat.h>
#include <tuple>

using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}
QString fixture(const QString &name) { return QStringLiteral(SOURCE_DIR "/tests/fixtures/step/") + name; }
QString writeFile(const QString &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir + "/" + name;
    QFileInfo parent(path);
    check(parent.dir().mkpath("."), "make " + name.toStdString());
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write " + name.toStdString());
    check(file.write(bytes) == bytes.size(), "write " + name.toStdString());
    file.close();
    return path;
}
// The stand-in helper: --import-step writes the body's output (progress lines and a package or a
// refusal), and --probe answers with the given JSON. The body can also hang (the cancel and
// deadline cases).
QString standIn(const QString &probeJson, const QString &importBody) {
    return QString(
        "#!/bin/sh\n"
        "for arg in \"$@\"; do\n"
        "    if [ \"$arg\" = \"--probe\" ]; then\n"
        "        cat <<'PROBE'\n%1\nPROBE\n"
        "        exit 0\n"
        "    fi\n"
        "done\n"
        "%2\n")
        .arg(probeJson, importBody);
}
QString executable(const QString &dir, const QString &name, const QString &script) {
    const QString path = writeFile(dir, name, script.toUtf8());
    check(::chmod(path.toLocal8Bit().constData(), 0755) == 0, "chmod " + name.toStdString());
    return path;
}
// A conversion package the test builds itself, as the real helper's tessellator would write it:
// an assembly root and one box solid, a 20 x 20 x 20 millimetre box (0.02 metres in the element's
// own frame, the package's own unit) with its twelve triangles.
QByteArray stepBoxPackage(const QString &sourceFile) {
    IfcPackage package;
    package.engine = QJsonObject{{"workerVersion", "stand-in"}, {"ifcOpenShell", "stand-in"}};
    package.source = QJsonObject{{"file", sourceFile}, {"bytes", 12345}, {"schema", "ap214"}};
    package.units = QJsonObject{{"length", "mm"}, {"applied", "declared"}, {"mmPerUnit", 1.0},
                                {"metresPerUnit", 0.001}};
    package.report = QJsonObject{{"counts", QJsonObject{{"solids", 1}}}, {"notImported", QJsonArray()}, {"notices", QJsonArray()}};
    package.meshes = {ifc_fixture::cubeMesh(0.02)};
    auto root = ifc_fixture::node(1, "STEP_ASSEMBLY", "box.2p", -1, IfcNodeRole::Assembly);
    auto solid = ifc_fixture::node(2, "ADVANCED_FACE", "the box", 0, IfcNodeRole::Element, 0);
    // The classes ride the real worker's own package layout: IFC-labelled nodes, the STEP
    // meaning in the role (the package's decoder requires IFC classes).
    root.className = "IfcStepAssembly";
    solid.className = "IfcStepSolid";
    root.step = 0;
    solid.step = 0;
    package.nodes = {root, solid};
    return encodeIfcPackage(package);
}
// The stand-in that writes the package and plays the helper's progress protocol (the same lines
// the helper prints for one shape: read, the slots, the shapes as they are tessellated, write,
// done, the result line).
QString packageBody(const QString &packageFile) {
    return QStringLiteral(R"(
    out=""
    prev=""
    for arg in "$@"; do
        if [ "$prev" = "--output" ]; then out="$arg"; fi
        prev="$arg"
    done
    [ -n "$out" ] || exit 9
    echo 'P phase read'
    echo 'P tasks 1'
    echo 'P at 0'
    echo 'P phase tessellate'
    echo 'P at 0'
    echo 'P phase write'
    echo 'P done 1'
    cp 'PACKAGE' "$out" || exit 4
    echo 'R {"status":"ok","counts":{"solids":1}}'
    exit 0
)")
        .replace("PACKAGE", packageFile); // the package path is quoted in the body (it has no quotes of its own)
}
QString refuseBody(const QString &code, const QString &message) {
    // The helper's own refusal line for --import-step (runStepImportPackage): status, code, message.
    return QStringLiteral(R"(
    echo 'R {"status":"refused","code":"%1","message":"%2"}'
    exit 3
)")
        .arg(code, message);
}
// The application options for the stand-in, with a bounded scratch.
StepWorkerOptions workerOptions(const QString &path, int deadlineSeconds = 30) {
    StepWorkerOptions options;
    options.workerPath = path;
    options.deadlineSeconds = deadlineSeconds;
    options.addressSpaceBytes = 0;
    return options;
}
// A copy of the fixture the stand-in answers from (its argument is the copy, not the source).
QString stagedPackage(const QString &dir, const QString &name, const QByteArray &bytes) {
    return writeFile(dir, name, bytes);
}
// R098.i: the stand-in tessellates base 0 (the root group) and base 1 (the box), as the helper does (P at before each
// base; P excluded for a base the application left out), and stops on the base named `stopAt` unless the application
// left it out. `stopCommand` is how it stops. Each run appends a line to `runLog`, so the runs are counted.
QString restartBody(const QString &packageFile, const QString &runLog, int stopAt, const QString &stopCommand) {
    return QStringLiteral(R"(
    out=""
    prev=""
    excluded=" "
    for arg in "$@"; do
        if [ "$prev" = "--output" ]; then out="$arg"; fi
        if [ "$prev" = "--exclude-step" ]; then excluded="$excluded$arg "; fi
        prev="$arg"
    done
    [ -n "$out" ] || exit 9
    echo run >> '@RUNLOG@'
    echo 'P phase read'
    echo 'P tasks 2'
    echo 'P phase tessellate'
    echo 'P at 0'
    echo 'P at @STOPAT@'
    case "$excluded" in
    *" @STOPAT@ "*) echo 'P excluded @STOPAT@' ;;
    *) @STOPCOMMAND@ ;;
    esac
    echo 'P phase write'
    echo 'P done 1'
    cp '@PACKAGE@' "$out" || exit 4
    echo 'R {"status":"ok","counts":{"solids":1}}'
    exit 0
)")
        .replace("@RUNLOG@", runLog)
        .replace("@STOPCOMMAND@", stopCommand)
        .replace("@PACKAGE@", packageFile)
        .replace("@STOPAT@", QString::number(stopAt));
}
// R098.i: a stand-in that stops in the phase `phase` (before the tessellation, or after it), with `base` the one it says it is on
// (a negative base: no P at line). Each run appends a line to `runLog`.
QString phaseStopBody(const QString &phase, int base, const QString &runLog) {
    const QString at = base >= 0 ? QStringLiteral("    echo 'P at @BASE@'\n").replace("@BASE@", QString::number(base)) : QString();
    return QStringLiteral(R"(
    echo run >> '@RUNLOG@'
    echo 'P phase read'
    echo 'P tasks 2'
    echo 'P phase @PHASE@'
@AT@    echo 'stand-in: crashing with SIGSEGV' >&2
    kill -SEGV $$
)")
        .replace("@RUNLOG@", runLog)
        .replace("@PHASE@", phase)
        .replace("@AT@", at);
}
// R098.i: a stand-in that stops on base N, where N is the number of bases the application has left out so far: each run stops
// on a base the last one did not, so the restarts run out.
QString everyRunBody(const QString &runLog) {
    return QStringLiteral(R"(
    n=0
    prev=""
    for arg in "$@"; do
        if [ "$prev" = "--exclude-step" ]; then n=$((n + 1)); fi
        prev="$arg"
    done
    echo run >> '@RUNLOG@'
    echo 'P phase tessellate'
    echo "P at $n"
    echo 'stand-in: crashing with SIGSEGV' >&2
    kill -SEGV $$
)")
        .replace("@RUNLOG@", runLog);
}
// The number of runs a stand-in logged (one line each).
int runCount(const QString &runLog) {
    QFile log(runLog);
    if (!log.open(QIODevice::ReadOnly))
        return 0;
    return int(log.readAll().count('\n'));
}
// A partial import's report: the base left out (its step), the reason of its stop, and the restarts counted.
void checkPartial(const QJsonObject &report, int base, const std::string &reason, int restarts, const std::string &where) {
    const QJsonArray excluded = report["excluded"].toArray();
    check(excluded.size() == 1, where + ": one base is left out, got " + std::to_string(excluded.size()));
    check(excluded.size() == 1 && excluded[0].toObject()["step"].toInt(-1) == base,
          where + ": the base left out is " + std::to_string(base));
    check(excluded.size() == 1 && excluded[0].toObject()["reason"].toString() == QString::fromStdString(reason),
          where + ": the reason of the stop is " + reason);
    bool restarted = false;
    for (const QJsonValue &value : report["notices"].toArray()) {
        const QJsonObject notice = value.toObject();
        if (notice["code"].toString() == "restarted")
            restarted = notice["count"].toInt(-1) == restarts;
    }
    check(restarted, where + ": the restarted notice counts " + std::to_string(restarts));
    check(report["engine"].toObject()["restarts"].toInt(-1) == restarts,
          where + ": the engine counts " + std::to_string(restarts) + " restart(s)");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    // The stand-ins.
    const QString staged = writeFile(root.path(), "box.skifc", stepBoxPackage("box.step"));
    check(!staged.isEmpty(), "the package was written");
    const QString available =
        executable(root.path(), "worker-ok", standIn("{\"ok\":true}", packageBody(root.path() + "/box.skifc")));
    const QString refused = executable(
        root.path(), "worker-refused",
        standIn("{\"ok\":true}", refuseBody("units", "the file declares no length unit and none was chosen")));
    const QString hang = executable(root.path(), "worker-hang",
                                    standIn("{\"ok\":true}", QStringLiteral("    exec /bin/sleep 300\n")));
    try {
        // C1: the cheap checks accept the STEP fixtures, and name the declared unit when the raw text declares one.
        {
            const QStringList names = {"box-negative.step", "inch-units.step", "mirror-box.step",
                                       "sketchyup-step-assembly.step", "sketchyup-step-box.step",
                                       "sketchyup-step-cylinder.step", "spaced-units.step"};
            for (const QString &name : names) {
                const QString path = fixture(name);
                check(QFile::exists(path), "the fixture is present: " + name.toStdString());
                const StepScan scan = checkStepSource(path);
                check(scan.schema == "ap214" || scan.schema == "ap203" || scan.schema == "ap242",
                      "a fixture is one of the accepted schemas: " + scan.schema.toStdString());
            }
            // The box fixture declares the SI millimetre (the writer's own declaration); the app's scan says so.
            const StepScan box = checkStepSource(fixture("sketchyup-step-box.step"));
            check(box.unitDeclared && box.unit == "millimetre",
                  "the box's declared unit is read from the raw text: " + box.unit.toStdString());
            // The inch fixture: the raw text's first declaration is its SI base (the helper's policy reads
            // the first; the inch is what the file's context applies - the helper's modelLengthUnit says inch),
            // so the app's scan says millimetre, and the dialog's option is the unit a unit-less file would take.
            const StepScan inch = checkStepSource(fixture("inch-units.step"));
            check(inch.unitDeclared && inch.unit == "millimetre",
                  "the inch file's raw-text unit is read as the helper's policy reads it: " + inch.unit.toStdString());
        }
        // C2: the cheap checks refuse what the helper's would refuse, with the helper's codes.
        auto refusedCode = [](const QString &path) {
            try {
                checkStepSource(path);
                return QString();
            } catch (const IfcImportError &error) {
                return QString::fromStdString(error.code());
            }
        };
        {
            const QString notes = writeFile(root.path(), "notes.txt", "This is a note file, not a STEP file.\n");
            check(refusedCode(notes) == "notStep", "a note file is refused as notStep");
            check(refusedCode(root.path() + "/absent.step") == "notAFile", "a missing file is refused as notAFile");
            // A compressed .step is refused as a format (the ZIP magic).
            const QString zipped = writeFile(root.path(), "zip.step", QByteArray("PK\x03\x04", 4));
            check(refusedCode(zipped) == "unsupportedFormat", "a compressed .step is refused as a format");
            // A file that is only its header (no FILE_SCHEMA) is refused as noSchema.
            const QString noSchema =
                writeFile(root.path(), "noschema.step",
                          "ISO-10303-21;\nHEADER;\nFILE_NAME('x.step','','(','',')','','');\nDATA;\nENDSEC;\nEND-ISO-10303-21;\n");
            check(refusedCode(noSchema) == "noSchema", "a file without a FILE_SCHEMA is refused as noSchema");
            // A file with a schema the import does not accept is refused as unsupportedSchema.
            const QString otherSchema =
                writeFile(root.path(), "other.step",
                          "ISO-10303-21;\nHEADER;\nFILE_SCHEMA(('AP230'));\nDATA;\n1,STEP_TYPE('x');\nENDSEC;\nEND-ISO-10303-21;\n");
            check(refusedCode(otherSchema) == "unsupportedSchema",
                  "a schema the import does not accept is refused");
        }
        // C3: an import through the stand-in -- the package converts, the report is the step's own.
        {
            StepWorkerOptions worker = workerOptions(available);
            const auto result = importStepFile(fixture("sketchyup-step-box.step"), worker);
            const Document &document = result.document;
            const QJsonObject report = result.report;
            // The assembly root becomes a group body and the solid its one geometry body.
            check(document.bodies().size() == 2, "the box converted to a group and one body");
            const Body *box = nullptr;
            for (const auto &[id, body] : document.bodies())
                if (body->surface.faces.size() > 0)
                    box = body.get();
            check(box != nullptr, "one of the two bodies is the box's geometry");
            check(box->surface.faces.size() == 6, "the box has its six faces");
            check(box->surface.vertices.size() >= 8, "the box has its eight vertices");
            check(report["geometry"].toObject()["groups"].toInteger() == 1, "the assembly is the group");
            // The report is the step's own: the format is corrected to STEP (the helper's
            // converter writes IFC's, and the application owns the correction), and the
            // tessellation is the helper's own (deflection and angle, as the application chose
            // them, and the result).
            check(report["format"].toString() == "STEP", "the report says STEP");
            check(report["tessellation"].toObject()["deflectionMm"].toDouble() == 1.0,
                  "the tessellation's deflection is the application's (1 millimetre)");
            check(report["tessellation"].toObject()["angleDeg"].toDouble() == 30.0,
                  "the tessellation's angle is the application's (30 degrees)");
            check(report["units"].toObject()["length"].toString() == "mm", "the report carries the package's unit");
            check(report["step"].toObject()["omitted"].toInt(0) >= 0, "the report carries the step's own counts");
            // The accounting is the application's, and it holds: every entity is a converted body
            // or a supporting one (nothing is silently dropped), and the bar reads them.
            const QJsonObject accounting = report["accounting"].toObject();
            check(accounting["entities"].toInt(0) == 2, "the package's two entities (the root and the box)");
            check(accounting["converted"].toInt(0) == 1, "one body converted");
            check(accounting["supporting"].toInt(0) == 1, "the root supports the body");
            check(accounting["omitted"].toInt(0) == 0, "nothing is omitted");
            // The engine is the helper's (the stand-in's), with the application's run facts merged.
            check(report["engine"].toObject().contains("workerVersion"), "the engine carries the helper's version");
        }
        // C4: a refusal is the helper's own: the code and the message are the helper's, and the
        // report says why (the source and its unit), so the sheet can show what the helper said.
        {
            StepWorkerOptions worker = workerOptions(refused);
            worker.lengthUnitOverride = "mm";
            try {
                importStepFile(fixture("sketchyup-step-box.step"), worker);
                check(false, "the stand-in's refusal is expected");
            } catch (const IfcImportError &error) {
                check(error.code() == "units", "the refusal's code is the helper's: " + error.code());
                check(std::string(error.what()).find("length unit") != std::string::npos, "the refusal's words are the helper's");
                const QJsonObject report = error.report();
                check(report["format"].toString() == "STEP", "the refusal report says STEP");
                check(report["direction"].toString() == "import", "the refusal report is an import");
                check(report["source"].toObject().contains("sha256"), "the refusal report names the source");
            }
        }
        // C5: a deadline the helper does not meet is a stop, not a crash: the run ends, the model
        // is not touched, and the reason is the deadline's.
        {
            StepWorkerOptions worker = workerOptions(hang, 1);
            try {
                importStepFile(fixture("sketchyup-step-box.step"), worker);
                check(false, "the hanging stand-in is expected to time out");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerFailed", "a timed-out helper is a failed run: " + error.code());
                check(std::string(error.what()).find("timeout") != std::string::npos,
                      "the stop's reason is the deadline's timeout");
            }
        }
        // C6: a cancel is a stop: the helper is asked to end, and the run ends as cancelled.
        {
            StepWorkerOptions worker = workerOptions(hang, 30);
            worker.cancelled = [] { return true; };
            try {
                importStepFile(fixture("sketchyup-step-box.step"), worker);
                check(false, "the cancelled run is expected to fail");
            } catch (const IfcImportError &error) {
                check(error.code() == "cancelled", "a cancel is a stop: " + error.code());
            }
        }
        // C7: a helper the application cannot find is a refusal of the application's own (the
        // import does not start; the dialog and the sheet say what is missing).
        {
            StepWorkerOptions worker = workerOptions(root.path() + "/absent-worker");
            try {
                importStepFile(fixture("sketchyup-step-box.step"), worker);
                check(false, "a missing helper is expected to fail");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerMissing", "a missing helper is refused as workerMissing");
            }
        }
        // R098.e, E6 (ADR 0172 section 7): a file over 128 MiB is refused by the application before any helper starts. The
        // stand-in hangs, so a helper that did start would end in a timeout instead. A file of exactly 128 MiB passes the
        // application's check (sparse files: no disk is used).
        {
            const QString big = root.path() + "/over-limit.step";
            QFile file(big);
            check(file.open(QIODevice::WriteOnly), "the over-limit file opens");
            check(file.write("ISO-10303-21;\nHEADER;\nFILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }'));\nENDSEC;\n"
                             "DATA;\nENDSEC;\nEND-ISO-10303-21;\n") > 0, "the over-limit header is written");
            check(file.resize(128 * 1024 * 1024), "the file is resized to 128 MiB");
            file.close();
            check(checkStepSource(big).schema == "ap214", "128 MiB exactly passes the application's size check");
            QFile grown(big);
            check(grown.open(QIODevice::ReadWrite) && grown.resize(128 * 1024 * 1024 + 1), "the file is resized to 128 MiB + 1 byte");
            grown.close();
            try {
                importStepFile(big, workerOptions(hang, 30));
                check(false, "a file of 128 MiB + 1 byte is refused");
            } catch (const IfcImportError &error) {
                check(error.code() == "tooLarge", "128 MiB + 1 byte is refused as tooLarge: " + error.code());
                const std::string words(error.what());
                check(words.find("134217729") != std::string::npos && words.find("128 MiB") != std::string::npos,
                      "the refusal names the size and the limit: " + words);
            }
        }
        // E6: a helper killed by SIGSEGV mid-run is a failed import with its log tail; the import throws and creates nothing.
        {
            const QString crash = executable(
                root.path(), "worker-segv",
                standIn("{\"ok\":true}", QStringLiteral("    echo 'P phase tessellate'\n"
                                                        "    echo 'stand-in: crashing with SIGSEGV' >&2\n"
                                                        "    kill -SEGV $$\n")));
            try {
                importStepFile(fixture("sketchyup-step-box.step"), workerOptions(crash, 30));
                check(false, "a helper killed by SIGSEGV is a failed import");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerFailed", "SIGSEGV: a failed import: " + error.code());
                check(std::string(error.what()).find("crash") != std::string::npos, "SIGSEGV: the reason is a crash");
                const QJsonObject report = error.report();
                check(report["logTail"].toString().contains("SIGSEGV"), "SIGSEGV: the log tail is in the report");
                check(report["engine"].toObject()["stops"].toArray().first().toString() == "crash (tessellate)",
                      "SIGSEGV: the stop names the phase");
            }
        }
        // E6: a helper over RLIMIT_AS. The stand-in applies the limit the application passes (--rlimit-as) and then asks for
        // more memory than that, as an allocation of the helper would; its log tail says so, as the helper's does.
        {
            const QString memory = executable(
                root.path(), "worker-as",
                standIn("{\"ok\":true}", QStringLiteral(R"(
    limit=""
    prev=""
    for arg in "$@"; do
        if [ "$prev" = "--rlimit-as" ]; then limit="$arg"; fi
        prev="$arg"
    done
    echo 'P phase tessellate'
    ulimit -v $((limit / 1024))
    python3 -c 'import sys
try:
    bytearray(512 * 1024 * 1024)
except MemoryError:
    sys.stderr.write("std::bad_alloc: the address space limit was reached\n")
    sys.exit(1)
sys.exit(0)' || exit 1
)")));
            StepWorkerOptions options = workerOptions(memory, 30);
            options.addressSpaceBytes = qint64(256) * 1024 * 1024;
            try {
                importStepFile(fixture("sketchyup-step-box.step"), options);
                check(false, "a helper over RLIMIT_AS is a failed import");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerFailed", "RLIMIT_AS: a failed import: " + error.code());
                check(std::string(error.what()).find("memory") != std::string::npos, "RLIMIT_AS: the reason is memory");
                const QJsonObject report = error.report();
                check(report["logTail"].toString().contains("bad_alloc"), "RLIMIT_AS: the log tail is in the report");
            }
        }
        // E6: a helper over RLIMIT_CPU is ended by SIGXCPU. The stand-in sets its own two-second CPU limit (the helper receives
        // --rlimit-cpu, four deadlines; this deadline is six seconds of wall time, so the stand-in's limit comes first).
        {
            const QString cpu = executable(
                root.path(), "worker-cpu",
                standIn("{\"ok\":true}", QStringLiteral("    echo 'P phase tessellate'\n"
                                                        "    echo 'stand-in: cpu limit of two seconds' >&2\n"
                                                        "    ulimit -t 2\n"
                                                        "    while :; do :; done\n")));
            try {
                importStepFile(fixture("sketchyup-step-box.step"), workerOptions(cpu, 6));
                check(false, "a helper over RLIMIT_CPU is a failed import");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerFailed", "RLIMIT_CPU: a failed import: " + error.code());
                const QJsonObject report = error.report();
                check(report["logTail"].toString().contains("cpu limit"), "RLIMIT_CPU: the log tail is in the report");
                check(report["engine"].toObject()["stops"].toArray().first().toString() == "crash (tessellate)",
                      "RLIMIT_CPU: the stop is a crash in the tessellation");
            }
        }
        // R098.e, E8 (ADR 0172 section 5): a message is at most 256 bytes and the report at most 64 KiB. The stand-in refuses with
        // a 5,000-byte message; the application keeps the message to 240 bytes, the bound its helper's texts are clipped to too
        // (step_import.cpp clips the kernel messages to 240 bytes).
        {
            const QString verbose = executable(root.path(), "worker-long",
                                               standIn("{\"ok\":true}", refuseBody("units", QString(5000, QChar('m')))));
            try {
                importStepFile(fixture("sketchyup-step-box.step"), workerOptions(verbose));
                check(false, "a refusal with a long message is refused");
            } catch (const IfcImportError &error) {
                const auto size = std::string(error.what()).size();
                check(size <= 256, "E8: the message is at most 256 bytes, got " + std::to_string(size));
                check(QJsonDocument(error.report()).toJson(QJsonDocument::Compact).size() <= 64 * 1024,
                      "E8: the report is within 64 KiB");
            }
        }
        // R098.i (ADR 0172 sections 5 and 6, owner decision 7): a stop in the tessellation of a base is restarted with that
        // base left out, at most three times, and the import is partial and names the base. Every stand-in here logs each
        // run, so the restarts are counted, not only the result. The deadline is 5 s: the stops are not timed (the
        // SIGSEGV, the SIGSYS and the read and write stops end at once); only the hang waits for the deadline.
        {
            // SIGSEGV on base 1: one restart, the partial import names base 1.
            const QString runs = root.path() + "/runs-segv.log";
            const QString worker = executable(root.path(), "worker-restart-segv",
                                              standIn("{\"ok\":true}", restartBody(staged, runs, 1, "kill -SEGV $$")));
            const IfcImport partial = importStepFile(fixture("sketchyup-step-box.step"), workerOptions(worker, 30));
            check(runCount(runs) == 2, "SIGSEGV at base 1: two runs, got " + std::to_string(runCount(runs)));
            checkPartial(partial.report, 1, "crash", 1, "SIGSEGV at base 1");
            check(partial.document.bodies().size() == 2, "SIGSEGV at base 1: the document holds the group and the box");
            check(partial.report["format"].toString() == "STEP", "SIGSEGV at base 1: the report says STEP");
        }
        {
            // A hang past the deadline on base 1: the same restart, the stop's reason is the deadline's.
            const QString runs = root.path() + "/runs-timeout.log";
            const QString worker = executable(root.path(), "worker-restart-timeout",
                                              standIn("{\"ok\":true}", restartBody(staged, runs, 1, "exec /bin/sleep 300")));
            const IfcImport partial = importStepFile(fixture("sketchyup-step-box.step"), workerOptions(worker, 5));
            check(runCount(runs) == 2, "timeout at base 1: two runs, got " + std::to_string(runCount(runs)));
            checkPartial(partial.report, 1, "timeout", 1, "timeout at base 1");
            check(partial.document.bodies().size() == 2, "timeout at base 1: the document holds the group and the box");
        }
        {
            // A SIGSYS is the sandbox's kill: never restarted, refused whole, as sandboxViolation.
            const QString runs = root.path() + "/runs-sigsys.log";
            const QString worker = executable(root.path(), "worker-sigsys",
                                              standIn("{\"ok\":true}", restartBody(staged, runs, 1, "kill -SYS $$")));
            try {
                importStepFile(fixture("sketchyup-step-box.step"), workerOptions(worker, 30));
                check(false, "a SIGSYS kill is a refused import");
            } catch (const IfcImportError &error) {
                check(error.code() == "sandboxViolation", "SIGSYS: refused as sandboxViolation: " + error.code());
                check(runCount(runs) == 1, "SIGSYS: not restarted, one run, got " + std::to_string(runCount(runs)));
            }
        }
        {
            // A stop in the read phase has no base to leave out: no restart, refused whole.
            const QString runs = root.path() + "/runs-read.log";
            const QString worker = executable(root.path(), "worker-read-stop",
                                              standIn("{\"ok\":true}", phaseStopBody("read", -1, runs)));
            try {
                importStepFile(fixture("sketchyup-step-box.step"), workerOptions(worker, 30));
                check(false, "a stop in the read phase is a failed import");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerFailed", "read-phase stop: a failed import: " + error.code());
                check(std::string(error.what()).find("crash") != std::string::npos, "read-phase stop: the reason is a crash");
                check(runCount(runs) == 1, "read-phase stop: not restarted, one run, got " + std::to_string(runCount(runs)));
                const QJsonObject report = error.report();
                check(report["excluded"].toArray().isEmpty(), "read-phase stop: no base is left out");
            }
        }
        {
            // A stop in the write phase (after the tessellation, with a base named) is not restarted either.
            const QString runs = root.path() + "/runs-write.log";
            const QString worker = executable(root.path(), "worker-write-stop",
                                              standIn("{\"ok\":true}", phaseStopBody("write", 1, runs)));
            try {
                importStepFile(fixture("sketchyup-step-box.step"), workerOptions(worker, 30));
                check(false, "a stop in the write phase is a failed import");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerFailed", "write-phase stop: a failed import: " + error.code());
                check(runCount(runs) == 1, "write-phase stop: not restarted, one run, got " + std::to_string(runCount(runs)));
            }
        }
        {
            // A helper that stops every time: three restarts, then a failed import with the stops, the bases left out
            // and the log tail. Four runs in all, and the fourth stop is not restarted.
            const QString runs = root.path() + "/runs-every.log";
            const QString worker = executable(root.path(), "worker-every-run", standIn("{\"ok\":true}", everyRunBody(runs)));
            try {
                importStepFile(fixture("sketchyup-step-box.step"), workerOptions(worker, 30));
                check(false, "a helper that stops every time is a failed import");
            } catch (const IfcImportError &error) {
                check(error.code() == "workerFailed", "every run: a failed import: " + error.code());
                check(std::string(error.what()).find("3 restart(s)") != std::string::npos,
                      "every run: the failure names the three restarts: " + std::string(error.what()));
                check(runCount(runs) == 4, "every run: four runs (three restarts), got " + std::to_string(runCount(runs)));
                const QJsonObject report = error.report();
                check(report["engine"].toObject()["restarts"].toInt(-1) == 3, "every run: the engine counts three restarts");
                check(report["engine"].toObject()["stops"].toArray().size() == 4, "every run: four stops are reported");
                const QJsonArray excluded = report["excluded"].toArray();
                check(excluded.size() == 3, "every run: three bases are left out, got " + std::to_string(excluded.size()));
                for (int i = 0; i < excluded.size(); ++i)
                    check(excluded[i].toObject()["step"].toInt(-1) == i, "every run: base " + std::to_string(i) + " is left out");
                check(report["logTail"].toString().contains("SIGSEGV"), "every run: the log tail is in the report");
            }
        }
    } catch (const std::exception &error) {
        std::cerr << "failure: " << error.what() << '\n';
        return 1;
    }
#ifdef CLI_PATH
    // L1: the application's CLI: --import-step is a standalone operation with its own options.
    {
        const auto cli = QString::fromUtf8(CLI_PATH);
        const auto run = [&cli](const QStringList &args) {
            QProcess process;
            process.start(cli, args);
            const bool finished = process.waitForStarted(10000) && process.waitForFinished(60000);
            return std::make_tuple(finished, process.exitStatus(), process.exitCode(),
                                  process.readAllStandardOutput(), process.readAllStandardError());
        };
        // L1a: an import (the stand-in writes the package) with its own options; the result is the
        // compact JSON the ADR's section 8 says (status, importReport, nativeFile).
        {
            const QString out = root.path() + "/out.sketchyup";
            auto [ok, status, code, outText, errText] =
                run({"--import-step", fixture("sketchyup-step-box.step"),
                     "--step-worker", available,
                     "--step-deflection-mm", "0.5",
                     "--step-angle-deg", "45",
                     "--step-length-unit", "mm",
                     "--output", out});
            check(ok && status == QProcess::NormalExit && code == 0,
                  "the CLI's import runs: " + errText.toStdString());
            const QJsonObject result = QJsonDocument::fromJson(outText).object();
            check(result["status"].toString() == "imported", "the CLI says imported: " + outText.toStdString());
            check(result["importReport"].toObject()["format"].toString() == "STEP", "the CLI's report says STEP");
            check(QFile::exists(out), "the CLI wrote the native file");
        }
        // L1b: a refusal is an INPUT_ERROR with the helper's code and the helper's words (the
        // CLI's own error line, on its error stream, as the IFC import's is).
        {
            auto [ok, status, code, outText, errText] =
                run({"--import-step", fixture("sketchyup-step-box.step"), "--step-worker", refused});
            check(ok && status == QProcess::NormalExit && code == 1, "the CLI's refusal ends 1");
            const QJsonObject result = QJsonDocument::fromJson(errText).object();
            check(result["status"].toString() == "failed" && result["code"].toString() == "INPUT_ERROR",
                  "the CLI's refusal is an INPUT_ERROR: " + errText.toStdString());
            check(result["details"].toObject()["stepCode"].toString() == "units",
                  "the CLI's refusal carries the helper's code");
            check(result["error"].toString().contains("length unit"),
                  "the CLI's refusal carries the helper's words");
        }
        // L1c: the step options are validated like the IFC's are (bounds, and the unit's spelling).
        {
            auto [ok, status, code, outText, errText] =
                run({"--import-step", fixture("sketchyup-step-box.step"), "--step-worker", available,
                     "--step-deflection-mm", "0"});
            check(ok && code == 1 && QJsonDocument::fromJson(errText).object()["code"].toString() == "INVALID_REQUEST",
                  "the CLI rejects a deflection below its bound: " + errText.toStdString());
            auto [ok2, status2, code2, outText2, errText2] =
                run({"--import-step", fixture("sketchyup-step-box.step"), "--step-worker", available,
                     "--step-angle-deg", "0"});
            check(ok2 && code2 == 1 && QJsonDocument::fromJson(errText2).object()["code"].toString() == "INVALID_REQUEST",
                  "the CLI rejects a non-positive angle");
            auto [ok3, status3, code3, outText3, errText3] =
                run({"--import-step", fixture("sketchyup-step-box.step"), "--step-worker", available,
                     "--step-length-unit", "pc"});
            check(ok3 && code3 == 1 && QJsonDocument::fromJson(errText3).object()["code"].toString() == "INVALID_REQUEST",
                  "the CLI rejects a unit it does not know");
        }
        // L1d: the step options require --import-step, and the operation is standalone (the IFC's
        // options are not mixed in).
        {
            auto [ok, status, code, outText, errText] = run({"--step-length-unit", "mm"});
            check(ok && code == 1 && QJsonDocument::fromJson(errText).object()["code"].toString() == "INVALID_REQUEST",
                  "a step option without the operation is refused: " + errText.toStdString());
            auto [ok2, status2, code2, outText2, errText2] =
                run({"--import-step", fixture("sketchyup-step-box.step"), "--step-worker", available,
                     "--ifc-skip-spaces"});
            check(ok2 && code2 == 1 && QJsonDocument::fromJson(errText2).object()["code"].toString() == "INVALID_REQUEST",
                  "the step import is standalone (the IFC's options are not mixed in): " + errText2.toStdString());
        }
    }
#endif
    std::cout << "step import tests: all checks passed\n";
    return 0;
}
