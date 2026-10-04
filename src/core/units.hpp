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
inline std::string_view defaultLengthUnit(DisplayUnit unit) {
    return unit == DisplayUnit::FeetInches ? "ft" : unitCode(unit);
}
} // namespace sketchy
