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
// Fixed decimals keep trailing zeros, omit digit grouping and never show "-0".
inline QString fixedDecimal(double value, int precision) {
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    const auto scale = std::pow(10., precision);
    if (std::round(std::abs(value) * scale) == 0)
        value = 0;
    return locale.toString(value, 'f', precision);
}
// Full (fullDisplayPrecision) is the historical trimmed, round-trip-safe form.
inline QString displayLength(double meters, DisplayUnit unit, int precision) {
    requireDisplayPrecision(unit, precision);
    const bool full = precision == fullDisplayPrecision;
    if (unit == DisplayUnit::Meters)
        return (full ? displayDecimal(meters, 8) : fixedDecimal(meters, precision)) + " m";
    if (unit == DisplayUnit::Millimeters)
        return (full ? displayDecimal(meters * 1000, 5) : fixedDecimal(meters * 1000, precision)) +
               " mm";
    if (full) {
        const auto inches = std::round(std::abs(meters) / .0254 * 1e6) / 1e6;
        const auto feet = std::floor(inches / 12);
        return (meters < 0 && inches > 0 ? "-" : "") + QString::number(qint64(feet)) + "' " +
               displayDecimal(inches - feet * 12, 6) + "\"";
    }
    // Round once in whole units of the last shown inch decimal, so a rounded
    // 12 inches carries into feet.
    const auto scale = qint64(std::llround(std::pow(10., precision)));
    const auto units = qint64(std::llround(std::abs(meters) / .0254 * double(scale)));
    const auto feet = units / (12 * scale);
    const auto inches = double(units % (12 * scale)) / double(scale);
    return (meters < 0 && units > 0 ? "-" : "") + QString::number(feet) + "' " +
           fixedDecimal(inches, precision) + "\"";
}
inline QString displayMeasure(double value, int power, DisplayUnit unit, int precision) {
    requireDisplayPrecision(unit, precision);
    const auto factor = unit == DisplayUnit::Millimeters  ? 1000.
                        : unit == DisplayUnit::FeetInches ? 1 / .3048
                                                          : 1.;
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    const auto scaled = value * std::pow(factor, power);
    return (precision == fullDisplayPrecision ? locale.toString(scaled, 'g', 8)
                                              : fixedDecimal(scaled, precision)) +
           " " + inputUnit(unit) + (power == 2 ? "²" : "³");
}
// Combo sample: 1.2345678 m in the given unit and precision.
inline QString precisionSample(DisplayUnit unit, int precision) {
    const auto sample = displayLength(1.2345678, unit, precision);
    return precision == fullDisplayPrecision ? "Full (" + sample + ")" : sample;
}
} // namespace sketchy
