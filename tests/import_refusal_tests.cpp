// R105.a (ADR 0179 sections 7.3 and 8, V1): the named refusals for the downloadable file types
// SketchyUp cannot import (a ZIP archive, COLLADA, USDZ and FBX). The check is the application's
// own and reads only the file's first bytes, so these cases run without any helper and without the
// network. The command line is checked too, through the built sketchyup-cli, as the import suites
// check theirs.
//
// The fixtures are self-made (tests/fixtures/import-refusal/SOURCE.txt). The misnamed files are
// written here, so that the content check is tested without a binary checked into the repository.

#include "io/import_refusal.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>

using namespace sketchy;

namespace {

int passed = 0;

void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
    ++passed;
}

bool contains(const QString &text, const QString &part) { return text.contains(part); }

QString fixture(const QString &name) {
    return QStringLiteral(SOURCE_DIR "/tests/fixtures/import-refusal/") + name;
}

QString writeFile(const QString &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir + "/" + name;
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write " + name.toStdString());
    check(file.write(bytes) == bytes.size(), "write " + name.toStdString());
    file.close();
    return path;
}

// The refusal's code, or "" when the file is not refused.
std::string codeOf(const QString &path) {
    const auto refusal = refusedImportFile(path);
    return refusal ? refusal->code : std::string();
}

const QByteArray zipMagic("PK\x03\x04", 4);
const QByteArray fbxMagic("Kaydara FBX Binary  \x00", 21);
// Bytes with NUL in them: a literal would stop at the first NUL, so each is sized.
QByteArray bytes(const char *data, int size) { return QByteArray(data, size); }

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    check(root.isValid(), "a temporary directory");
    const QString dir = root.path();

    try {
        // A: each fixture is refused, by its named format, with the reason and (for the three that
        // have one) advice.
        {
            const auto zip = refusedImportFile(fixture("wrapped-box.zip"));
            check(zip && zip->code == "zipArchive", "a ZIP is refused as a ZIP archive");
            check(zip->message ==
                      "This is a ZIP archive. Extract it, then import the model file inside it.",
                  "the ZIP refusal names the archive and the step, in the words ADR 0179 gives: " +
                      zip->message.toStdString());
            check(zip->whatToDo.isEmpty(), "the ZIP refusal needs no second advice line");

            const auto dae = refusedImportFile(fixture("minimal.dae"));
            check(dae && dae->code == "collada", "a COLLADA .dae is refused as COLLADA");
            check(contains(dae->message, "COLLADA (.dae)") &&
                      contains(dae->message, "not imported"),
                  "the COLLADA refusal names the format and that it is not imported");
            check(contains(dae->whatToDo, ".skp") && contains(dae->whatToDo, "newer"),
                  "the COLLADA advice says to download a newer .skp version where the source "
                  "offers one");

            const auto usdz = refusedImportFile(fixture("stub.usdz"));
            check(usdz && usdz->code == "usdz", "a USDZ file is refused as USDZ");
            check(contains(usdz->message, "USDZ (.usdz)") &&
                      contains(usdz->message, "not imported"),
                  "the USDZ refusal names the format");
            check(contains(usdz->whatToDo, "glTF") &&
                      contains(usdz->whatToDo, "File › Import GLB/glTF"),
                  "the USDZ advice points at the glTF import the product has");

            const auto fbx = refusedImportFile(fixture("stub.fbx"));
            check(fbx && fbx->code == "fbx", "an FBX file is refused as FBX");
            check(contains(fbx->message, "FBX (.fbx)") && contains(fbx->message, "not imported"),
                  "the FBX refusal names the format");
            check(contains(fbx->whatToDo, "glTF"), "the FBX advice points at the glTF import");
        }

        // B: the extraction the ZIP refusal asks for. The archive's member is the repository's own
        // box.skp, and that file passes the check, so the advised next step works.
        {
            const QByteArray zipBytes = QFile(fixture("wrapped-box.zip")).readAll();
            check(zipBytes.startsWith(zipMagic), "the wrapper is a ZIP archive");
            check(zipBytes.contains("box.skp"),
                  "the wrapper holds box.skp, the file the user extracts");
            check(codeOf(QStringLiteral(SOURCE_DIR "/tests/fixtures/skp/box.skp")).empty(),
                  "the extracted model file is not refused");
        }

        // C: files the importers read are left alone, whatever their bytes say.
        {
            check(codeOf(QStringLiteral(SOURCE_DIR "/tests/fixtures/skp/box.skp")).empty(),
                  "box.skp is readable");
            check(
                codeOf(QStringLiteral(SOURCE_DIR "/tests/fixtures/step/box-negative.step")).empty(),
                "a STEP file is readable");
            check(codeOf(QStringLiteral(SOURCE_DIR "/tests/fixtures/ifc/units-m.ifc")).empty(),
                  "an IFC file is readable");
            check(codeOf(writeFile(dir, "ascii.stl", "solid t\nendsolid t\n")).empty(),
                  "an ASCII STL is readable");
            check(codeOf(writeFile(dir, "mesh.obj", "v 0 0 0\n")).empty(),
                  "an OBJ file is readable");
            check(codeOf(writeFile(dir, "scene.glb", bytes("glTF\x02\x00\x00\x00", 8))).empty(),
                  "a GLB file is readable");
            check(codeOf(writeFile(dir, "plan.dxf", "0\nSECTION\n")).empty(),
                  "a DXF file is readable");
            check(codeOf(writeFile(dir, "ground.xyz", "1 2 3\n")).empty(),
                  "a terrain point file is readable");
            check(codeOf(writeFile(dir, "shape.formline", "{}")).empty(),
                  "a formline file is readable");
            check(codeOf(writeFile(dir, "unlucky.stl", zipMagic + bytes("\x00\x01", 2))).empty(),
                  "a readable extension is not refused on its bytes alone");
            check(codeOf(writeFile(dir, "bad-ifczip.ifczip", zipMagic)).empty(),
                  "an .ifczip is left to the IFC importer's own named refusal");
            check(refusedImportFile(QStringLiteral("/no/such/file.bin")) == std::nullopt,
                  "a file that cannot be read is the importer's to report");
        }

        // D: the same formats under another name are still named from their first bytes.
        {
            check(codeOf(writeFile(dir, "download", zipMagic + bytes("\x14\x00", 2))) ==
                      "zipArchive",
                  "a ZIP without an extension is named by its bytes");
            check(codeOf(writeFile(dir, "download (2).bin", zipMagic + bytes("\x14\x00", 2))) ==
                      "zipArchive",
                  "a ZIP under an unknown extension is named by its bytes");
            check(codeOf(writeFile(dir, "empty.zip-like",
                                   QByteArray("PK\x05\x06", 4) + QByteArray(18, '\0'))) ==
                      "zipArchive",
                  "an empty ZIP archive is named by its end record");
            check(codeOf(writeFile(dir, "model.bin", fbxMagic + bytes("\x1a\x00", 2))) == "fbx",
                  "a binary FBX under an unknown extension is named by its header");
            check(codeOf(writeFile(dir, "ascii.bin", "; FBX 7.4.0 project file\n")) == "fbx",
                  "an ASCII FBX is named by its comment line");
            check(codeOf(writeFile(dir, "model.xml",
                                   "<?xml version=\"1.0\"?>\n<!-- exported -->\n<COLLADA "
                                   "version=\"1.4.1\"/>\n")) == "collada",
                  "a COLLADA document under another name is named by its root element");
            check(codeOf(writeFile(dir, "notes.bin", "just some text\n")).empty(),
                  "a file of no refused type is left to its importer");
            // The extension is decided first, and case does not matter.
            check(codeOf(QStringLiteral("/no/such/MODEL.DAE")) == "collada",
                  "an upper-case .DAE is COLLADA");
            check(codeOf(QStringLiteral("/no/such/MODEL.USDZ")) == "usdz",
                  "an upper-case .USDZ is USDZ");
            check(codeOf(QStringLiteral("/no/such/MODEL.FBX")) == "fbx",
                  "an upper-case .FBX is FBX");
            check(codeOf(QStringLiteral("/no/such/MODEL.ZIP")) == "zipArchive",
                  "an upper-case .ZIP is a ZIP archive");
        }

        // E: the command line names the same refusals, before any importer runs, and leaves nothing
        // behind.
#ifdef CLI_PATH
        {
            const auto cli = QString::fromUtf8(CLI_PATH);
            const auto run = [&cli](const QStringList &args) {
                QProcess process;
                process.start(cli, args);
                const bool finished =
                    process.waitForStarted(10000) && process.waitForFinished(60000);
                return std::make_tuple(finished, process.exitStatus(), process.exitCode(),
                                       process.readAllStandardOutput(),
                                       process.readAllStandardError());
            };
            // Each refused file, on each import option, gives the same named failure and nothing
            // else.
            struct Case {
                QStringList args;
                QString refusal; // the refusal's code, as the details carry it
            };
            const QString zipMessage =
                "This is a ZIP archive. Extract it, then import the model file inside it.";
            const QList<Case> cases{
                {{"--import-stl", fixture("wrapped-box.zip")}, "zipArchive"},
                {{"--import-skp", fixture("minimal.dae")}, "collada"},
                {{"--import-ifc", fixture("stub.usdz")}, "usdz"},
                {{"--import-step", fixture("stub.fbx")}, "fbx"},
                {{"--import-gltf", fixture("wrapped-box.zip")}, "zipArchive"},
                {{"--import-obj", fixture("minimal.dae")}, "collada"},
                {{"--import-dxf", fixture("stub.fbx")}, "fbx"},
            };
            for (const auto &item : cases) {
                const QString out = dir + "/cli-" + item.refusal + ".sketchyup";
                auto args = item.args;
                args << "--output" << out;
                auto [finished, status, exitCode, outText, errText] = run(args);
                const std::string label = (item.args.first() + " " + item.refusal).toStdString();
                check(finished && status == QProcess::NormalExit, "the CLI finishes: " + label);
                check(exitCode == 1, "a refused import exits 1: " + label);
                check(outText.isEmpty(), "a refused import prints nothing on stdout: " + label);
                const QJsonObject failure = QJsonDocument::fromJson(errText).object();
                check(failure["status"].toString() == "failed",
                      "a refused import reports failed: " + label);
                check(failure["code"].toString() == "INPUT_ERROR",
                      "a refused import uses the input error code: " + label);
                check(failure["details"].toObject()["refusalCode"].toString() == item.refusal,
                      "the refusal's code is in the details: " + label);
                const QString error = failure["error"].toString();
                check(error.contains("not imported") || error == zipMessage,
                      "the refusal names the format: " + label + " " + error.toStdString());
                check(!QFileInfo::exists(out), "a refused import writes no native file: " + label);
            }
            // The message the CLI gives is the one the dialog shows (the refusal names the format,
            // not a generic failure).
            {
                auto [finished, status, exitCode, outText, errText] =
                    run({"--import-stl", fixture("wrapped-box.zip")});
                check(finished && status == QProcess::NormalExit && exitCode == 1,
                      "the ZIP refusal through --import-stl exits 1");
                check(QJsonDocument::fromJson(errText).object()["error"].toString() == zipMessage,
                      "the CLI's ZIP refusal uses the dialog's words");
            }
            // A readable file is not refused by the check: the failure, if any, is the importer's
            // own.
            {
                auto [finished, status, exitCode, outText, errText] =
                    run({"--import-stl", writeFile(dir, "ok.stl", "solid t\nendsolid t\n"),
                         "--stl-weld", "none"});
                check(finished && status == QProcess::NormalExit,
                      "the CLI finishes for a readable STL");
                check(!contains(QString::fromUtf8(errText), "refusalCode"),
                      "a readable STL is not refused by the check");
            }
        }
#endif
    } catch (const std::exception &error) {
        std::cerr << "import_refusal_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "import_refusal_tests: " << passed << " checks passed\n";
    return 0;
}
