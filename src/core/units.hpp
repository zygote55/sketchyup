#pragma once
#include <stdexcept>
#include <string_view>
namespace sketchy {
enum class DisplayUnit { Meters, Millimeters, FeetInches };
inline std::string_view unitCode(DisplayUnit unit) {
    switch (unit) {
    case DisplayUnit::Meters:
        return "m";
    case DisplayUnit::Millimeters:
        return "mm";
    case DisplayUnit::FeetInches:
        return "ft-in";
    }
    throw std::runtime_error("Unknown document display units");
}
inline DisplayUnit parseDisplayUnit(std::string_view code) {
    for (auto unit : {DisplayUnit::Meters, DisplayUnit::Millimeters, DisplayUnit::FeetInches})
        if (unitCode(unit) == code)
            return unit;
    throw std::runtime_error("Document units must be m, mm or ft-in");
}
// Display precision: decimal places shown for lengths (the inches part for
// feet/inches) and for areas/volumes in the squared/cubed unit. Full keeps the
// historical trimmed, round-trip-safe formatting.
inline constexpr int fullDisplayPrecision = -1;
inline int maxDisplayPrecision(DisplayUnit unit) {
    return unit == DisplayUnit::Meters ? 6 : 3;
}
inline bool validDisplayPrecision(DisplayUnit unit, int precision) {
    return precision == fullDisplayPrecision ||
           (precision >= 0 && precision <= maxDisplayPrecision(unit));
}
inline void requireDisplayPrecision(DisplayUnit unit, int precision) {
    unitCode(unit);
    if (!validDisplayPrecision(unit, precision))
        throw std::runtime_error(unit == DisplayUnit::Meters
                                     ? "Display precision must be full or 0-6 for m"
                                 : unit == DisplayUnit::Millimeters
                                     ? "Display precision must be full or 0-3 for mm"
                                     : "Display precision must be full or 0-3 for ft-in");
}
inline std::string_view defaultLengthUnit(DisplayUnit unit) {
    return unit == DisplayUnit::FeetInches ? "ft" : unitCode(unit);
}
} // namespace sketchy
