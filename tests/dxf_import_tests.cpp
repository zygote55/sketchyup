#include "io/dxf_import.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-8, "Independent native DXF geometry oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid DXF conversion must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto path = QStringLiteral(SOURCE_DIR "/tests/fixtures/dxf/ezdxf-r2018.dxf");
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "Reference source");
        const auto bytes = file.readAll();
        file.close();
        auto result = loadDxf(path);
        auto &doc = result.document;
        check(doc.bodies().size() == 4 && result.report["curves"].toInt() == 3 &&
                  doc.history().total == 1,
              "Four reference entities and three analytic curves in one edit");
        const auto &line = *doc.bodies().at(1);
        check(line.locked && !doc.tags().at(line.tag)->visible &&
                  doc.tags().at(line.tag)->name == "Walls",
              "Layer assignment, locking and visibility retained");
        check(line.surface.wires.size() == 1 && line.surface.faces.empty(),
              "Lines stay editable wires");
        near(line.surface.vertices.at(line.surface.wires[0][1]).x, 6);
        near(line.surface.vertices.at(line.surface.wires[0][1]).y, 4);
        const auto &arc = doc.bodies().at(2)->curves.begin()->second;
        near(arc.center.x, 1);
        near(arc.center.y, 2);
        near(arc.radius, 1);
        near(arc.sweepAngle, 20 * std::numbers::pi / 180);
        check(arc.edges.size() == 6 && arc.segments == 6,
              "Default curve chord resolution is explicit");
        const auto &circle = doc.bodies().at(3)->curves.begin()->second;
        check(circle.kind == CurveKind::Circle && circle.edges.size() == 96,
              "Circle analytic metadata bound to closed wire ring");
        near(circle.radius, .5);
        const auto &poly = *doc.bodies().at(4);
        check(poly.curves.size() == 1 && poly.surface.wires.size() == 50 &&
                  poly.surface.faces.empty(),
              "Bulged closed polyline remains wires without implicit fill");
        near(poly.curves.begin()->second.radius, 1);
        near(poly.curves.begin()->second.sweepAngle, std::numbers::pi);
        check(result.report["maximumChordDeviationMetres"].toDouble() > 0,
              "Chord approximation error reported");
        const auto encoded = encodeDocument(doc);
        check(encodeDocument(decodeDocument(encoded)) == encoded,
              "Native geometry, tags and analytic curves persist");
        const auto bodies = doc.bodies();
        const auto tags = doc.tags();
        doc.undo();
        check(doc.bodies().empty() && doc.tags().empty(), "One undo removes all imported data");
        doc.redo();
        for (const auto &[id, b] : bodies)
            check(*doc.bodies().at(id) == *b, "Redo restores entity records");
        for (const auto &[id, t] : tags)
            check(*doc.tags().at(id) == *t, "Redo restores layer records");
        check(file.open(QIODevice::ReadOnly) && file.readAll() == bytes, "Source unchanged");
        const auto source = parseDxf(bytes);
        rejects([&] { importDxf(source, 11); });
        rejects([&] { importDxf(source, 257); });
        auto malformed = source;
        malformed.entities[0].layer = "absent";
        rejects([&] { importDxf(malformed); });
        malformed = source;
        malformed.entities[1].segments[0].sweep = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { importDxf(malformed); });
        malformed = source;
        malformed.entities[1].segments[0].end.x += .01;
        rejects([&] { importDxf(malformed); });
        malformed = source;
        malformed.entities.clear();
        rejects([&] { importDxf(malformed); });
        const auto legacy =
            loadDxf(QStringLiteral(SOURCE_DIR "/tests/fixtures/dxf/ezdxf-r12.dxf"), {.001}, 48);
        check(legacy.report["curves"].toInt() == 2 &&
                  legacy.document.bodies().at(4)->surface.wires.size() == 3,
              "R12 polyline conversion and caller resolution");
#ifdef CLI_PATH
        QTemporaryDir directory;
        check(directory.isValid(), "CLI scratch");
        const auto cli = QString::fromUtf8(CLI_PATH);
        auto invoke = [&](QStringList args, bool success) {
            QProcess p;
            p.start(cli, args);
            check(p.waitForStarted(10000) && p.waitForFinished(30000), "DXF CLI completed");
            check((p.exitCode() == 0) == success, "DXF CLI expected status");
            const auto bytes = p.readAllStandardOutput() + p.readAllStandardError();
            check(QJsonDocument::fromJson(bytes).isObject(), "DXF CLI structured result");
        };
        const auto native = directory.filePath("new.sketchyup"),
                   output = directory.filePath("new.dxf");
        const QStringList base{"--import-dxf", path, "--dxf-unit", "header"};
        invoke(base, true);
        invoke(base + QStringList{"--output", native}, true);
        invoke(base + QStringList{"--output", native}, false);
        invoke({"--import-dxf", path}, false);
        invoke(base + QStringList{"--dxf-segments", "11"}, false);
        invoke(base + QStringList{"--dxf-segments", "256"}, true);
        invoke(base + QStringList{"--stl-up", "z"}, false);
        invoke({"--dxf-unit", "mm"}, false);
        const QStringList exported{"--export-dxf", output, "--input", native, "--dxf-unit", "mm"};
        invoke(exported, true);
        invoke(exported, false);
        invoke(exported + QStringList{"--dxf-segments", "48"}, false);
        invoke({"--export-dxf", directory.filePath("bad.dxf"), "--input", native, "--dxf-unit",
                "header"},
               false);
        check(loadDxf(output).report["curves"].toInt() == 3, "CLI export retains analytic curves");
        check(file.seek(0) && file.readAll() == bytes, "CLI leaves source DXF untouched");
#endif
        std::cout << "Native DXF independent dimensions, layers, analytic curves, undo, "
                     "persistence and bounds passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
