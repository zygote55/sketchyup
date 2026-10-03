#include "automation/commands.hpp"
#include "automation/measurements.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    check(std::abs(a - b) < 1e-10, "Parsed measurement differs from expected value");
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        near(parseLength("350cm"), 3.5);
        near(parseLength("12'6\""), 3.81);
        near(parseLength("3/4\""), .01905);
        near(parseLength("-1 3/4 in"), -.04445);
        near(parseLength("-0'6\""), -.1524);
        near(parseLength("12'"), 3.6576);
        near(parseLength("25", "mm"), .025);
        near(parseLength("1/2m"), .5);
        near(parseAngle("180deg"), std::numbers::pi);
        near(parseAngle("90°"), std::numbers::pi / 2);
        near(parseAngle("1/2rad"), .5);
        near(parseAngle("90"), std::numbers::pi / 2);
        const QLocale comma(QLocale::German, QLocale::Germany);
        near(parseLength("1,5m", "m", comma), 1.5);
        auto dimensions = parseMeasurements("1,5;2,75", "m", comma);
        check(dimensions.values == std::vector<double>{1.5, 2.75},
              "Comma-decimal dimensions use semicolon");
        auto absolute = parseMeasurements("[1,2,0]");
        check(absolute.kind == MeasurementKind::AbsolutePoint &&
                  absolute.values == std::vector<double>{1, 2, 0},
              "Absolute coordinates");
        auto relative = parseMeasurements("<100cm;2m;-3/4in>");
        check(relative.kind == MeasurementKind::RelativePoint, "Relative coordinates");
        near(relative.values[2], -.01905);
        check(parseMeasurements("24s").kind == MeasurementKind::Segments, "Segments syntax");
        check(parseMeasurements("x5").kind == MeasurementKind::Copies, "Copies syntax");
        check(parseMeasurements("/5").kind == MeasurementKind::Divisions, "Divide syntax");
        for (const auto &text : {"", "nan", "inf", "1/0in", "[1,2]", "[1,2,3>", "1,,2", "12'13\"",
                                 "1e999m", "-3/-4in", "1000001m", "x0"})
            rejects([&] { parseMeasurements(text); });
        rejects([&] { parseLength("1.000", "m", comma); });
        rejects([&] { parseMeasurements(QString(1025, '1')); });
        Document doc;
        auto faceCommand = [](double width) {
            return QJsonObject{
                {"command", "geometry.face"},
                {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{width, 0, 0},
                                                QJsonArray{width, 1, 0}, QJsonArray{0, 1, 0}}}}};
        };
        auto request = [&](double width) {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(doc.identity())},
                               {"expectedRevision", QString::number(doc.revision())},
                               {"commands", QJsonArray{faceCommand(width)}}};
        };
        executeBatch(doc, request(2));
        const auto token = doc.amendmentStamp();
        const auto bytes = encodeDocument(doc);
        auto proposed = previewAmend(doc, token, request(3));
        check(encodeDocument(doc) == bytes, "Amend preview is nonmutating");
        auto result = executeAmend(doc, token, request(3));
        check(proposed["changes"] == result["changes"] &&
                  proposed["document"] == result["document"],
              "Amend preview and commit agree");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc), "Amended geometry persists exactly");
        check(!reopened.canAmend(doc.amendmentStamp()), "Amend tokens are session-local");
        doc.undo();
        check(doc.bodies().empty() && !doc.canUndo(), "Command amendment retains one undo item");
        std::cout << "Units, fractions, locale coordinates, angles and atomic amendment command "
                     "previews passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
