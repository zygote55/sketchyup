#pragma once
#include <QLocale>
#include <QString>
#include <vector>
namespace sketchy {
enum class MeasurementKind { Values, AbsolutePoint, RelativePoint, Segments, Copies, Divisions };
struct MeasurementInput {
    MeasurementKind kind{MeasurementKind::Values};
    std::vector<double> values;
};
double parseLength(QString text, const QString &defaultUnit = "m",
                   const QLocale &locale = QLocale::c());
double parseAngle(QString text, const QString &defaultUnit = "deg",
                  const QLocale &locale = QLocale::c());
// R095.e2: the values of a Measurements-box site position "@E, N[, Z]" (the text after the
// leading @): two or three lengths separated as coordinates are (comma, or semicolon where the
// decimal separator is a comma), in `defaultUnit` unless suffixed.
std::vector<double> parseSitePosition(QString text, const QString &defaultUnit = "m",
                                      const QLocale &locale = QLocale::c());
MeasurementInput parseMeasurements(QString text, const QString &defaultUnit = "m",
                                   const QLocale &locale = QLocale::c());
} // namespace sketchy
