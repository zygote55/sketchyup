#pragma once
#include "geometry/surface.hpp"
#include <cstdint>
#include <string_view>

namespace sketchy {
inline constexpr std::string_view solarAlgorithm = "noaa-meeus-geometric-v1";
// Explicit civil time and UTC offset. No OS time zone or daylight-saving inference.
struct SolarCivilTime {
    int year{2026}, month{6}, day{21}, hour{12}, minute{}, second{}, utcOffsetMinutes{};
    bool operator==(const SolarCivilTime &) const = default;
};
struct SolarSettings {
    bool enabled{}, shadows{true};
    double latitude{}, longitude{}, northDegrees{};
    SolarCivilTime time;
    bool operator==(const SolarSettings &) const = default;
    void validate() const;
};
struct SolarPosition {
    Vec3 direction; // Unit vector toward the sun, native Z-up; default north is +Y.
    double elevationDegrees{}, azimuthDegrees{}, declinationDegrees{}, equationOfTimeMinutes{};
    bool azimuthDefined{}, aboveHorizon{};
};
// Gregorian UTC seconds since 1970; supported resolved UTC years are 1901–2099.
std::int64_t solarUtcSeconds(const SolarCivilTime &time);
SolarPosition solarPosition(const SolarSettings &settings);
} // namespace sketchy
