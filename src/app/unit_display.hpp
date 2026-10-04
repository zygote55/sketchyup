#pragma once
#include "core/units.hpp"
#include <QLocale>
#include <QString>
#include <cmath>
namespace sketchy {
inline QString inputUnit(DisplayUnit unit) {
    return QString::fromLatin1(defaultLengthUnit(unit).data());
}
inline QString unitName(DisplayUnit unit) {
    switch (unit) {
    case DisplayUnit::Meters:
        return "Meters";
    case DisplayUnit::Millimeters:
        return "Millimeters";
    case DisplayUnit::FeetInches:
        return "Feet and inches";
    }
    throw std::runtime_error("Unknown document units");
}
inline QString displayDecimal(double value, int precision) {
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    auto text = locale.toString(value, 'f', precision);
    while (text.endsWith('0'))
        text.chop(1);
    if (text.endsWith(locale.decimalPoint()))
        text.chop(locale.decimalPoint().size());
    if (text == "-0")
        text = "0";
    return text;
}
inline QString displayLength(double meters, DisplayUnit unit) {
    if (unit == DisplayUnit::Meters)
        return displayDecimal(meters, 8) + " m";
    if (unit == DisplayUnit::Millimeters)
        return displayDecimal(meters * 1000, 5) + " mm";
    unitCode(unit);
    const auto inches = std::round(std::abs(meters) / .0254 * 1e6) / 1e6;
    const auto feet = std::floor(inches / 12);
    return (meters < 0 && inches > 0 ? "-" : "") + QString::number(qint64(feet)) + "' " +
           displayDecimal(inches - feet * 12, 6) + "\"";
}
inline QString displayMeasure(double value, int power, DisplayUnit unit) {
    const auto factor = unit == DisplayUnit::Millimeters  ? 1000.
                        : unit == DisplayUnit::FeetInches ? 1 / .3048
                                                          : 1.;
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale.toString(value * std::pow(factor, power), 'g', 8) + " " + inputUnit(unit) +
           (power == 2 ? "²" : "³");
}
} // namespace sketchy
