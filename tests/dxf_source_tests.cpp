#include "io/dxf_source.hpp"
#include <QCoreApplication>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::isfinite(a) && std::abs(a - b) < 1e-8, "Independent DXF geometry oracle");
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Malformed DXF must reject");
}
QByteArray wrap(QByteArray entities, QByteArray units = "4", QByteArray version = "AC1032",
                QByteArray tables = {}) {
    return "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\n" + version + "\n9\n$INSUNITS\n70\n" + units +
           "\n0\nENDSEC\n" + tables + "0\nSECTION\n2\nENTITIES\n" + entities +
           "0\nENDSEC\n0\nEOF\n";
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const QByteArray line = "0\nLINE\n8\nWalls\n10\n0\n20\n0\n11\n6000\n21\n4000\n";
        const QByteArray arc = "0\nARC\n8\nCurves\n10\n1000\n20\n2000\n40\n1000\n50\n350\n51\n10\n";
        const QByteArray tables = "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n1\n0\nLAYER\n2\nW"
                                  "alls\n70\n5\n62\n-7\n0\nENDTAB\n0\nENDSEC\n";
        auto source = parseDxf(wrap(line + arc, "4", "AC1032", tables));
        check(source.entities.size() == 2 && source.layers.at("Walls").hidden &&
                  source.layers.at("Walls").locked,
              "Layer assignment and state");
        near(source.entities[0].segments[0].end.x, 6);
        near(source.entities[0].segments[0].end.y, 4);
        near(source.entities[1].segments[0].center.x, 1);
        near(source.entities[1].segments[0].center.y, 2);
        near(source.entities[1].segments[0].sweep, 20 * std::numbers::pi / 180);
        const QByteArray poly = "0\nLWPOLYLINE\n8\nCurves\n90\n3\n70\n1\n10\n0\n20\n0\n42\n1\n10\n2"
                                "000\n20\n0\n10\n2000\n20\n2000\n";
        const auto closed = parseDxf(wrap(poly));
        check(closed.entities[0].closed && closed.entities[0].segments.size() == 3,
              "Closed polyline segments");
        const auto &bulge = closed.entities[0].segments[0];
        near(bulge.center.x, 1);
        near(bulge.center.y, 0);
        near(bulge.sweep, std::numbers::pi);
        auto clockwise = poly;
        clockwise.replace("42\n1\n", "42\n-1\n");
        near(parseDxf(wrap(clockwise)).entities[0].segments[0].sweep, -std::numbers::pi);
        const QByteArray legacy = "0\nPOLYLINE\n8\nLegacy\n70\n0\n0\nVERTEX\n10\n0\n20\n0\n0\nVERTE"
                                  "X\n10\n12\n20\n0\n0\nSEQEND\n";
        near(parseDxf(wrap(legacy, "1", "AC1009")).entities[0].segments[0].end.x, .3048);
        const auto circle = parseDxf(wrap("0\nCIRCLE\n10\n0\n20\n0\n40\n1000\n"));
        check(circle.entities[0].closed, "Circle preserves closure");
        near(circle.entities[0].segments[0].sweep, 2 * std::numbers::pi);
        rejects([&] { parseDxf(wrap(line, "0")); });
        near(parseDxf(wrap(line, "0"), {.01}).entities[0].segments[0].end.x, 60);
        rejects([&] { parseDxf(wrap(line), {0}); });
        rejects([&] { parseDxf(wrap(line, "4", "AC9999")); });
        const auto omissions =
            parseDxf(wrap(line + "0\nINSERT\n2\n../../external.dwg\n0\nTEXT\n1\n(command)\n"));
        check(
            omissions.entities.size() == 1 &&
                omissions.report["omittedEntities"].toObject()["INSERT:unsupportedType"].toInt() ==
                    1,
            "Unsupported entities are reported without execution or resolution");
        auto wide = poly;
        wide.replace("70\n1\n", "70\n1\n43\n10\n");
        check(parseDxf(wrap(wide)).entities.empty(),
              "Wide polylines cannot silently lose geometry");
        auto nonXY = line;
        nonXY += "30\n1\n";
        check(parseDxf(wrap(nonXY)).entities.empty(), "Non-XY geometry explicitly omitted");
        auto extrusion = arc;
        extrusion += "230\n-1\n";
        check(parseDxf(wrap(extrusion)).entities.empty(), "Unsupported OCS explicitly omitted");
        auto mismatch = poly;
        mismatch.replace("90\n3\n", "90\n4\n");
        rejects([&] { parseDxf(wrap(mismatch)); });
        auto missing = poly;
        missing.replace("20\n2000\n", "");
        rejects([&] { parseDxf(wrap(missing)); });
        auto duplicate = line;
        duplicate += "11\n2\n";
        rejects([&] { parseDxf(wrap(duplicate)); });
        auto invalid = line;
        invalid.replace("6000", "nan");
        rejects([&] { parseDxf(wrap(invalid)); });
        auto absent = legacy;
        absent.replace("0\nSEQEND\n", "");
        rejects([&] { parseDxf(wrap(absent)); });
        rejects([&] { parseDxf(wrap(line) + "0\nLINE\n"); });
        auto truncated = wrap(line);
        truncated.chop(6);
        rejects([&] { parseDxf(truncated); });
        rejects([&] { parseDxf(QByteArray("AutoCAD Binary DXF\r\n\x1a\0", 22)); });
        auto malformed = wrap(line);
        malformed[20] = char(0xff);
        rejects([&] { parseDxf(malformed); });
        auto comment = wrap(line);
        comment.prepend("999\nignored comment\n");
        check(parseDxf(comment).entities.size() == 1, "DXF comment pairs ignored");
        rejects([&] { parseDxf(QByteArray(1000002, '\n')); });
        std::cout << "DXF lines, arcs, bulges, circles, layers, units and bounded malformed-input "
                     "checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
