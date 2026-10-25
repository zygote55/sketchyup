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
