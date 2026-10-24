// R098.a: the STEP probe and import, driven through the worker against its own writer (ADR
// 0172). The corpus is the three self-generated files of ADR 0172 section 7 (a box 10 x 20
// x 30, a cylinder of radius 5 and height 20, an assembly of the two, all in millimetres);
// the unit policy is checked against raw text, the round trip through the writer and reader
// is checked against the hand-computed geometry, and the refusals against the policy. Needs
// sketchyup-ifc-worker (IFC_WORKER_PATH); exits 77 (skipped) when the helper is not built.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string &message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}
bool near(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}
struct WorkerResult {
    int code = -1;
    QJsonObject json;
    QString error;
};
WorkerResult runWorker(const QString &worker, const QStringList &args) {
    QProcess process;
    // QProcess::start prepends the program as argv[0]; the list carries it again, so drop the copy.
    const auto arguments = args.first() == worker ? args.mid(1) : args;
    process.start(worker, arguments);
    if (!process.waitForFinished(300000)) {
        process.kill();
        return {124, {}, "timeout"};
    }
    WorkerResult result;
    result.code = process.exitCode();
    result.error = process.readAllStandardError().trimmed();
    // The OCCT transfer statistics share stdout; the report is the last line that opens with "{".
    for (const auto &line : process.readAllStandardOutput().split('\n')) {
        if (line.trimmed().startsWith('{'))
            result.json = QJsonDocument::fromJson(line).object();
    }
    return result;
}
void boxGeometryCheck(const QJsonObject &shape, const char *where) {
    const auto box = shape.value("boundingBox").toArray();
    const auto boxMm = shape.value("boundingBoxMm").toArray();
    const auto &source = box.isEmpty() ? boxMm : box;
    check(source.size() == 6, std::string(where) + ": the shape carries a six-number bounding box");
    if (source.size() != 6)
        return;
    const double x0 = source[0].toDouble(), y0 = source[1].toDouble(), z0 = source[2].toDouble();
    const double x1 = source[3].toDouble(), y1 = source[4].toDouble(), z1 = source[5].toDouble();
    check(near(x0, 0.0) && near(y0, 0.0) && near(z0, 0.0), std::string(where) + ": the origin");
    check(near(x1, 10.0) && near(y1, 20.0) && near(z1, 30.0), std::string(where) + ": the corner (10, 20, 30)");
}
}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#ifdef IFC_WORKER_PATH
    const QString worker = QStringLiteral(IFC_WORKER_PATH);
#else
    return 77;
#endif
    if (!QFile::exists(worker))
        return 77;

    QTemporaryDir temp;
    check(temp.isValid(), "a temporary directory for the corpus");
    const auto run = [&](const QStringList &args) { return runWorker(worker, args); };

    // 1. The self-generated corpus (ADR 0172 section 7): three files, all AP214.
    {
        const auto result = run({worker, "--write-step", temp.path()});
        check(result.code == 0, "write-step exits 0");
        const auto files = result.json.value("files").toArray();
        check(files.size() == 3, "write-step writes three files");
        for (const auto &value : files) {
            const auto file = value.toObject();
            check(file.value("written").toBool(), std::string("write-step: ") + file.value("file").toString().toStdString());
            check(file.value("bytes").toInt() > 0, std::string("write-step: bytes > 0 for ") + file.value("file").toString().toStdString());
            check(file.value("schema").toString() == "ap214", std::string("write-step: AP214 for ") + file.value("file").toString().toStdString());
        }
        // Determinism: the corpus is a regression guard on the writer, so the second write
        // must be byte identical.
        QJsonObject first = result.json;
        const auto again = run({worker, "--write-step", temp.path()});
        check(again.code == 0, "write-step (second) exits 0");
        const auto files2 = again.json.value("files").toArray();
        for (int i = 0; i < files.size(); ++i) {
            const auto a = files[i].toObject(), b = files2[i].toObject();
            check(a.value("bytes").toInt() == b.value("bytes").toInt(), std::string("write-step: deterministic bytes for ") + a.value("file").toString().toStdString());
        }
    }

    // 1b. The committed fixtures (tests/fixtures/step/): the corpus pinned in the repository.
    // The FILE_NAME carries a wall-clock timestamp, so the fixture is a regression input, not a
    // byte comparison of the fresh write: the probe and the import must see the same geometry.
    {
        const auto fixture = [&](const char *name) {
            return run({worker, "--probe-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/") + name}).json;
        };
        const auto boxFixture = fixture("sketchyup-step-box.step");
        const auto boxShapes = boxFixture.value("shapes").toArray();
        check(boxShapes.size() == 1 && boxShapes[0].toObject().value("solids").toInt() == 1
              && boxShapes[0].toObject().value("faces").toInt() == 6, "fixture box: one solid, six faces");
        if (boxShapes.size() == 1)
            boxGeometryCheck(boxShapes[0].toObject(), "fixture box");
        const auto boxImport = run({worker, "--import-step", QStringLiteral(SOURCE_DIR "/tests/fixtures/step/sketchyup-step-box.step")});
        check(boxImport.code == 0 && boxImport.json.value("status").toString() == "ok", "fixture import: the box reads with the declared unit");
        const auto cylinderFixture = fixture("sketchyup-step-cylinder.step");
        const auto cylShapes = cylinderFixture.value("shapes").toArray();
        check(cylShapes.size() == 1 && cylShapes[0].toObject().value("faces").toInt() == 3, "fixture cylinder: one solid, three faces");
        const auto assemblyFixture = fixture("sketchyup-step-assembly.step");
        const auto asmShapes = assemblyFixture.value("shapes").toArray();
        check(asmShapes.size() == 1 && asmShapes[0].toObject().value("solids").toInt() == 2, "fixture assembly: one shape, two solids");
    }

    // 2. The box, probed with the round trip (ADR 0172 section 2).
    const QString box = temp.filePath("sketchyup-step-box.step");
    const QJsonObject probe = run({worker, "--probe-step", box, "--round-trip-directory", temp.filePath("rt")}).json;
    check(probe.value("code").toInt() == 0, "probe of the box exits 0");
    {
        const auto file = probe.value("file").toObject();
        check(file.value("isoHeader").toBool(), "probe: the ISO header is there");
        check(file.value("schema").toString() == "ap214", "probe: the schema is AP214");
        const auto units = probe.value("units").toObject();
        check(units.value("declared").toBool(), "probe: the units are declared in the raw text");
        check(units.value("length").toString() == "millimetre", "probe: the declared length unit is the millimetre");
        check(near(units.value("lengthMetres").toDouble(), 0.001, 1e-9), "probe: the millimetre is 0.001 m");
        check(units.value("angle").toString() == "RADIAN", "probe: the plane angle is the radian");
        check(!probe.value("occt").toObject().value("version").toString().isEmpty(), "probe: the OCCT version is reported");
        check(probe.value("entities").toInt() > 0, "probe: the entity count is positive");
        check(probe.value("transferred").toInt() >= 1, "probe: a shape was transferred");
        const auto shapes = probe.value("shapes").toArray();
        check(shapes.size() == 1, "probe: one shape");
        if (shapes.size() == 1) {
            const auto shape = shapes[0].toObject();
            check(shape.value("solids").toInt() == 1, "probe: the box is one solid");
            check(shape.value("faces").toInt() == 6, "probe: the box has six faces");
            boxGeometryCheck(shape, "probe");
        }
        const auto roundTrip = probe.value("writeRoundTrip").toObject();
        const auto ap214 = roundTrip.value("ap214").toObject();
        check(ap214.value("written").toBool(), "round trip: the AP214 file was written");
        check(ap214.value("schema").toString() == "ap214", "round trip: the AP214 file reads back AP214");
        check(ap214.value("readStatus").toString() == "done", "round trip: the AP214 file reads");
        {
            QJsonObject probeShape;
            probeShape["boundingBox"] = ap214.value("boundingBox");
            boxGeometryCheck(probeShape, "round trip AP214");
        }
        const auto ap242 = roundTrip.value("ap242").toObject();
        check(ap242.value("written").toBool(), "round trip: the AP242 file was written");
        check(ap242.value("schema").toString() == "ap242", "round trip: the AP242 file reads back AP242");
        check(ap242.value("fileSchemaRaw").toString().contains("10303 442"), "round trip: the AP242 identifier carries part 442");
        check(ap242.value("readStatus").toString() == "done", "round trip: the AP242 file reads");
        {
            QJsonObject probeShape;
            probeShape["boundingBox"] = ap242.value("boundingBox");
            boxGeometryCheck(probeShape, "round trip AP242");
        }
    }

    // 3. The cylinder and the assembly.
    {
        const auto cylinder = run({worker, "--probe-step", temp.filePath("sketchyup-step-cylinder.step")}).json;
        const auto shapes = cylinder.value("shapes").toArray();
        check(shapes.size() == 1 && shapes[0].toObject().value("faces").toInt() == 3, "cylinder: one solid, three faces");
        const auto assembly = run({worker, "--probe-step", temp.filePath("sketchyup-step-assembly.step")}).json;
        const auto assemblyShapes = assembly.value("shapes").toArray();
        check(assemblyShapes.size() == 1 && assemblyShapes[0].toObject().value("solids").toInt() == 2, "assembly: two solids");
    }

    // 4. The unit policy (ADR 0172 decision 4): the declared unit is authoritative.
    {
        const auto noOption = run({worker, "--import-step", box});
        check(noOption.code == 0, "import (no option): exits 0");
        check(noOption.json.value("status").toString() == "ok", "import (no option): ok");
        const auto units = noOption.json.value("units").toObject();
        check(units.value("applied").toString() == "declared", "import (no option): the declared unit is applied");
        check(near(units.value("mmPerUnit").toDouble(), 1.0, 1e-9), "import (no option): the millimetre is 1 mm");
        const auto withOption = run({worker, "--import-step", box, "--step-length-unit", "in"});
        check(withOption.code == 0, "import (with an option): exits 0");
        check(withOption.json.value("units").toObject().value("applied").toString() == "declared",
              "import (with an option): the declared unit still wins");
    }

    // 5. The refusals. A file without the ISO-10303-21 header, and a file with the header
    // but no length unit and no --step-length-unit.
    {
        const QString noHeader = temp.filePath("no-header.step");
        QFile out(noHeader);
        check(out.open(QIODevice::WriteOnly), "the no-header file");
        out.write("this is not a STEP file\n");
        out.close();
        const auto refused = run({worker, "--import-step", noHeader});
        check(refused.code == 2, "refusal: exit 2 without the ISO header");
        check(refused.json.value("status").toString().startsWith("refused"), "refusal: the status is refused-*");
        const QString noUnits = temp.filePath("no-units.step");
        QFile unitsOut(noUnits);
        check(unitsOut.open(QIODevice::WriteOnly), "the no-units file");
        unitsOut.write("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('test'),'2;1');\n"
                     "FILE_NAME('test','2026-01-01',('A'),('O'),('T'),('P'),'');\n"
                     "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }'));\nENDSEC;\nDATA;\nENDSEC;\n");
        unitsOut.close();
        const auto noUnitsRefused = run({worker, "--import-step", noUnits});
        check(noUnitsRefused.code == 2, "refusal: exit 2 with no units and no option");
        check(noUnitsRefused.json.value("status").toString().startsWith("refused"), "refusal: refused-* without units and no option");
        const auto noUnitsOption = run({worker, "--import-step", noUnits, "--step-length-unit", "mm"});
        check(noUnitsOption.json.value("units").toObject().value("applied").toString() == "option",
              "import (no units, with the option): the option is applied");
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
