#include "core/terrain.hpp"
#include "io/dxf_export.hpp"
#include "io/terrain_import.hpp"
#include "io/terrain_source.hpp"
#include "terrain_import_fixtures.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <numbers>
#include <set>
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
// "XY" text pairs of every group-10/20 and 11/21 record in the ENTITIES section.
std::set<std::pair<std::string, std::string>> writtenXy(const QByteArray &bytes) {
    std::set<std::pair<std::string, std::string>> out;
    const auto lines = bytes.split('\n');
    bool entities = false;
    std::string x;
    for (qsizetype i = 0; i + 1 < lines.size(); i += 2) {
        const auto code = lines[i].trimmed(), value = lines[i + 1].trimmed();
        if (code == "2" && value == "ENTITIES")
            entities = true;
        if (!entities)
            continue;
        if (code == "10" || code == "11")
            x = value.toStdString();
        if (code == "20" || code == "21")
            out.insert({x, value.toStdString()});
    }
    return out;
}
// The decimal text a 4-decimal source value was written with (trailing zeros dropped).
std::string sourceText(double v) {
    auto text = QByteArray::number(v, 'f', 4);
    while (text.endsWith('0'))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    return text.toStdString();
}
void siteCoordinates() {
    // R095.e3 (ADR 0168 section 4): the UTM terrain fixture, imported with its proposed origin,
    // then its XY edges (model = source x scale - origin) exported with site coordinates.
    const auto set = parseDxfTerrain(terrain_fixture::terrainUtm());
    TerrainImportOptions options;
    options.roles = {{"BOUNDARY", TerrainImportRole::Boundary}, {"POND", TerrainImportRole::Hole}};
    const auto imported = runTerrainImport(set, options, std::nullopt);
    check(imported.ok && imported.establish, "UTM fixture imports with an established origin");
    const auto origin = *imported.establish;
    check(std::fmod(origin.easting, 100) == 0 && std::fmod(origin.northing, 100) == 0 &&
              std::abs(origin.easting - 512345) < 100 && std::abs(origin.northing - 5234567) < 100,
          "Proposed UTM origin is whole hundreds of metres");
    Document doc;
    std::set<std::pair<std::string, std::string>> expected;
    auto wires = std::make_shared<Body>();
    wires->id = 1;
    for (const auto &line : set.polylines) {
        const auto n = line.points.size();
        for (std::size_t i = 0; i < n; ++i) {
            const auto &a = line.points[i], &b = line.points[(i + 1) % n];
            if (i + 1 == n && !line.closed)
                break;
            const Vec3 pa{a.x * 1.0 - origin.easting, a.y * 1.0 - origin.northing, 0},
                pb{b.x * 1.0 - origin.easting, b.y * 1.0 - origin.northing, 0};
            if (pa.x == pb.x && pa.y == pb.y)
                continue;
            wires->surface.wires.push_back({wires->surface.vertex(pa), wires->surface.vertex(pb)});
            expected.insert({sourceText(a.x), sourceText(a.y)});
            expected.insert({sourceText(b.x), sourceText(b.y)});
        }
    }
    check(expected.size() > 1000, "UTM edges cover the fixture");
    wires->topology = Topology::rebuild(wires->surface, {});
    Edit edit{"UTM edges", {{1, nullptr, wires}}};
    edit.nextIdFloor = 2;
    doc.apply(std::move(edit), doc.revision());
    const auto beforeSite = exportDxf(doc, 1);
    setSiteOrigin(doc, origin);
    check(exportDxf(doc, 1).bytes == beforeSite.bytes && exportDxf(doc, 1, false).bytes ==
                                                             beforeSite.bytes,
          "Site coordinates are off by default and the bytes are unchanged");
    check(!exportDxf(doc, 1).report.contains("siteCoordinates"), "Default report has no site key");
    const auto site = exportDxf(doc, 1, true);
    check(site.report["siteCoordinates"].toBool() && site.bytes != beforeSite.bytes,
          "Site export is reported");
    const auto written = writtenXy(site.bytes);
    check(written == expected, "UTM export reproduces the source coordinates at their decimals");
    // Millimetres and feet convert back to the same four-decimal metre values.
    for (const auto &[unit, scale] : {std::pair{"mm", .001}, std::pair{"ft", .3048}}) {
        const auto converted = writtenXy(exportDxf(doc, scale, true).bytes);
        check(converted.size() == written.size(), unit);
        for (const auto &[x, y] : converted)
            for (const double v : {std::stod(x) * scale, std::stod(y) * scale})
                check(std::abs(v * 1e4 - std::round(v * 1e4)) < 1e-3 && v > 5e5, unit);
    }
    // Refusal without a site origin, and the notices.
    Document plain;
    plain.addWire(0, {0, 0, 0}, {1, 0, 0});
    rejects([&] { exportDxf(plain, 1, true); });
    try {
        exportDxf(plain, 1, true);
    } catch (const std::exception &e) {
        check(QString(e.what()).contains("site origin"), "Refusal names the missing site origin");
    }
    auto notice = [](const DxfExport &e, const char *needle) {
        for (const auto &n : e.report["notices"].toArray())
            if (n.toString().contains(needle))
                return true;
        return false;
    };
    check(!notice(exportDxf(plain, 1), "coordinates"), "No site notice without a site origin");
    check(notice(exportDxf(doc, 1), "model coordinates") && notice(site, "site coordinates"),
          "DXF reports say which coordinates were written");
    Document shortLine;
    setSiteOrigin(shortLine, origin);
    shortLine.addWire(0, {0, 0, 0}, {1, 0, 0});
    const auto text = exportDxf(shortLine, 1, true).bytes;
    check(text.contains("\n" + QByteArray::number(origin.easting, 'f', 0) + "\n") &&
              text.contains("\n" + QByteArray::number(origin.northing, 'f', 0) + "\n"),
          "Origin is added back to a short line");
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
        if (app.arguments().contains("--ezdxf-real")) {
            const auto python = qEnvironmentVariable("SKETCHYUP_DXF_TEST");
            if (python.isEmpty())
                return 77;
            QProcess consumer;
            consumer.start(python,
                           {QStringLiteral(SOURCE_DIR "/tests/dxf_reference_consumer.py"), output});
            check(consumer.waitForStarted(10000) && consumer.waitForFinished(30000) &&
                      consumer.exitStatus() == QProcess::NormalExit && consumer.exitCode() == 0,
                  "Independent ezdxf export consumer");
            check(consumer.readAllStandardOutput().contains("SKETCHYUP_DXF_CONSUMER_VERIFIED"),
                  "Independent DXF assertions completed");
            std::cout
                << "ezdxf independently verified units, layers, arcs, polylines and extents\n";
            return 0;
        }

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
        siteCoordinates();
        std::cout << "DXF analytic arcs, polylines, layers, transforms, contours and safe "
                     "publication passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
