#include "automation/measurements.hpp"
#include "core/transform.hpp"
#include <QRegularExpression>
namespace sketchy {
namespace {
double decimal(QString text, const QLocale &locale) {
    text = text.trimmed();
    const auto separator = locale.decimalPoint();
    if (separator != ".") {
        if (text.contains('.'))
            throw std::runtime_error(
                "Use the locale decimal separator; digit grouping is not accepted");
        text.replace(separator, ".");
    }
    static const QRegularExpression pattern(R"(^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)$)");
    bool valid = false;
    const auto value = text.toDouble(&valid);
    if (!pattern.match(text).hasMatch() || !valid || !std::isfinite(value))
        throw std::runtime_error("Expected a finite decimal number");
    return value;
}
double scalar(QString text, const QLocale &locale) {
    text = text.trimmed();
    if (!text.contains('/'))
        return decimal(text, locale);
    static const QRegularExpression fraction(R"(^([+-]?)(?:([0-9]+)\s+)?([0-9]+)\s*/\s*([0-9]+)$)");
    const auto match = fraction.match(text);
    if (!match.hasMatch())
        throw std::runtime_error("Expected a fraction such as 3/4 or 1 3/4");
    const auto denominator = decimal(match.captured(4), QLocale::c());
    if (denominator == 0)
        throw std::runtime_error("Fraction denominator cannot be zero");
    const auto whole = match.captured(2).isEmpty() ? 0 : decimal(match.captured(2), QLocale::c());
    return (match.captured(1) == "-" ? -1 : 1) *
           (whole + decimal(match.captured(3), QLocale::c()) / denominator);
}
std::pair<QString, QString> quantity(QString text, const QString &fallback) {
    static const QRegularExpression suffix(R"((mm|cm|m|ft|in|deg|rad|°|'|")$)",
                                           QRegularExpression::CaseInsensitiveOption);
    text = text.trimmed();
    const auto match = suffix.match(text);
    if (!match.hasMatch())
        return {text, fallback};
    auto unit = match.captured(1).toLower();
    if (unit == "'")
        unit = "ft";
    if (unit == "\"")
        unit = "in";
    if (unit == "°")
        unit = "deg";
    return {text.left(match.capturedStart()).trimmed(), unit};
}
void bounded(QString text) {
    if (text.isEmpty() || text.size() > 1024)
        throw std::runtime_error("Measurements must contain 1–1024 characters");
}
} // namespace
double parseLength(QString text, const QString &defaultUnit, const QLocale &locale) {
    text = text.trimmed();
    bounded(text);
    // A compound feet/inches entry has one sign applying to the complete length.
    const auto feet = text.indexOf('\'');
    if (feet >= 0 && feet < text.size() - 1) {
        auto sign = 1.0;
        if (text.startsWith('-')) {
            sign = -1;
            text.remove(0, 1);
        } else if (text.startsWith('+'))
            text.remove(0, 1);
        const auto mark = text.indexOf('\'');
        const auto footValue = scalar(text.left(mark), locale);
        auto inches = text.mid(mark + 1).trimmed();
        if (!inches.endsWith('"'))
            throw std::runtime_error("Compound feet/inches needs a final inch mark");
        inches.chop(1);
        const auto inchValue = scalar(inches, locale);
        if (footValue < 0 || inchValue < 0 || inchValue >= 12)
            throw std::runtime_error("Compound inches must be from zero up to twelve");
        return meters(sign * (footValue * 12 + inchValue), "in");
    }
    const auto [value, unit] = quantity(text, defaultUnit);
    return meters(scalar(value, locale), unit.toStdString());
}
double parseAngle(QString text, const QString &defaultUnit, const QLocale &locale) {
    bounded(text);
    const auto [value, unit] = quantity(text, defaultUnit);
    return radians(scalar(value, locale), unit.toStdString());
}
MeasurementInput parseMeasurements(QString text, const QString &defaultUnit,
                                   const QLocale &locale) {
    text = text.trimmed();
    bounded(text);
    MeasurementInput result;
    static const QRegularExpression count(R"(^(?:([x/])([0-9]+)|([0-9]+)s)$)",
                                          QRegularExpression::CaseInsensitiveOption);
    const auto special = count.match(text);
    if (special.hasMatch()) {
        bool ok = false;
        const auto value =
            (special.captured(3).isEmpty() ? special.captured(2) : special.captured(3)).toUInt(&ok);
        if (!ok || value == 0 || value > 100000)
            throw std::runtime_error("Count must be from 1 to 100000");
        result.kind = !special.captured(3).isEmpty() ? MeasurementKind::Segments
                      : special.captured(1) == "/"   ? MeasurementKind::Divisions
                                                     : MeasurementKind::Copies;
        result.values = {double(value)};
        return result;
    }
    if (text.startsWith('[') || text.startsWith('<')) {
        const auto close = text.startsWith('[') ? ']' : '>';
        if (!text.endsWith(close))
            throw std::runtime_error("Coordinate brackets do not match");
        result.kind =
            close == ']' ? MeasurementKind::AbsolutePoint : MeasurementKind::RelativePoint;
        text = text.mid(1, text.size() - 2);
    }
    const auto separator = locale.decimalPoint() == "," ? ';' : ',';
    // Semicolons are unambiguous in either locale.
    auto pieces = text.split(text.contains(';') ? ';' : separator, Qt::KeepEmptyParts);
    if (pieces.size() > 3)
        throw std::runtime_error("At most three measurement values are supported");
    for (auto piece : pieces)
        result.values.push_back(parseLength(piece, defaultUnit, locale));
    if (result.kind != MeasurementKind::Values && result.values.size() != 3)
        throw std::runtime_error("Coordinates need three values");
    return result;
}
} // namespace sketchy
