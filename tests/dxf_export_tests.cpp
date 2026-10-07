#include "io/dxf_export.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-8, "Independent DXF export oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Unsupported DXF export must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        check(dir.isValid(), "Scratch folder");
        const auto source =
            loadDxf(QStringLiteral(SOURCE_DIR "/tests/fixtures/dxf/ezdxf-r2018.dxf"));
        const auto before = encodeDocument(source.document);
        const auto history = source.document.history().total;
        const auto result = exportDxf(source.document, .001);
        check(result.report["arcsAndCircles"].toInt() == 3 &&
                  result.report["polylines"].toInt() == 1 && result.report["lines"].toInt() == 1,
              "Analytic curves and remaining connected edges export separately");
        const auto parsed = parseDxf(result.bytes);
        check(parsed.entities.size() == 5 && parsed.layers.at("Walls").hidden,
              "DXF entity and hidden layer retention");
        size_t arcs{}, circles{};
        bool expectedLine = false;
        for (const auto &e : parsed.entities) {
            if (e.type == "ARC") {
                ++arcs;
                const auto &s = e.segments[0];
                check(std::abs(s.sweep - std::numbers::pi) < 1e-8 ||
                          std::abs(s.sweep - 20 * std::numbers::pi / 180) < 1e-8,
                      "Exact exported analytic sweeps");
            }
            if (e.type == "CIRCLE") {
                ++circles;
                near(length(e.segments[0].start - e.segments[0].center), .5);
            }
            if (e.type == "LINE") {
                near(e.segments[0].end.x, 6);
                near(e.segments[0].end.y, 4);
                expectedLine = true;
            }
        }
        check(arcs == 2 && circles == 1 && expectedLine, "Independent DXF primitive inventory");
        const auto output = dir.filePath("new.dxf");
        writeDxfExport(result, output);
        rejects([&] { writeDxfExport(result, output); });
        QFile file(output);
        check(file.open(QIODevice::ReadOnly) && file.readAll() == result.bytes,
              "Exact non-replacing publication");
        auto corrupt = result;
        corrupt.bytes += ' ';
        rejects([&] { writeDxfExport(corrupt, dir.filePath("bad.dxf")); });
        check(QFile::link(dir.filePath("missing"), dir.filePath("link.dxf")),
              "Dangling output fixture");
        rejects([&] { writeDxfExport(result, dir.filePath("link.dxf")); });
        check(encodeDocument(source.document) == before &&
                  source.document.history().total == history,
              "Export does not mutate source or history");
        Document planar;
        auto body = std::make_shared<Body>();
        body->id = 1;
        body->surface.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        body->topology = Topology::rebuild(body->surface, {});
        Edit edit{"Plan", {{1, nullptr, body}}};
        edit.nextIdFloor = 2;
        planar.apply(std::move(edit), planar.revision());
        const auto face = exportDxf(planar, 1);
        check(face.report["polylines"].toInt() == 1 &&
                  face.report["losses"].toObject()["filledFacesExportedAsContours"].toInt() == 1 &&
                  parseDxf(face.bytes).entities[0].closed,
              "Faces become explicit closed contours");
        auto elevated = std::make_shared<Body>(*body);
        elevated->transform = Transform::translation({0, 0, 1});
        Edit move{"Elevated", {{1, planar.bodies().at(1), elevated}}};
        planar.apply(std::move(move), planar.revision());
        rejects([&] { exportDxf(planar, 1); });
        auto mirrored = source.document;
        auto arcBody = std::make_shared<Body>(*mirrored.bodies().at(2));
        arcBody->transform = Transform::scaling({-2, 2, 1});
        Edit mirror{"Reflect arc", {{2, mirrored.bodies().at(2), arcBody}}};
        mirrored.apply(std::move(mirror), mirrored.revision());
        const auto reflected = parseDxf(exportDxf(mirrored, 1).bytes);
        bool found = false;
        for (const auto &e : reflected.entities)
            if (e.type == "ARC" && std::abs(e.segments[0].center.x + 2) < 1e-8) {
                near(length(e.segments[0].start - e.segments[0].center), 2);
                near(e.segments[0].sweep, 20 * std::numbers::pi / 180);
                found = true;
            }
        check(found, "Reflection preserves circular geometry and positive DXF sweep");
        auto elliptical = std::make_shared<Body>(*arcBody);
        elliptical->transform = Transform::scaling({2, 1, 1});
        Edit stretch{"Ellipse", {{2, mirrored.bodies().at(2), elliptical}}};
        mirrored.apply(std::move(stretch), mirrored.revision());
        check(
            exportDxf(mirrored, 1).report["losses"].toObject()["curvesExportedAsChords"].toInt() ==
                1,
            "Non-circular transformed curves have explicit chord fallback");
        rejects([&] { exportDxf(source.document, 0); });
        rejects([&] { exportDxf(Document{}, 1); });
        std::cout << "DXF analytic arcs, polylines, layers, transforms, contours and safe "
                     "publication passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
