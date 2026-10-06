#include "solar/solar.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace sketchy {
namespace {
constexpr double radians = std::numbers::pi / 180;
double wrap(double degrees) {
    auto value = std::fmod(degrees, 360.);
    return value < 0 ? value + 360 : value;
}
double sinDegrees(double value) { return std::sin(value * radians); }
double cosDegrees(double value) { return std::cos(value * radians); }
} // namespace
std::int64_t solarUtcSeconds(const SolarCivilTime &time) {
    using namespace std::chrono;
    if (time.year < 1901 || time.year > 2099 || time.month < 1 || time.month > 12 || time.day < 1 ||
        time.day > 31 || time.hour < 0 || time.hour > 23 || time.minute < 0 || time.minute > 59 ||
        time.second < 0 || time.second > 59 || time.utcOffsetMinutes < -14 * 60 ||
        time.utcOffsetMinutes > 14 * 60)
        throw std::runtime_error(
            "Sun study requires a valid date, time and UTC offset (−14 to +14 hours)");
    const year_month_day date{year{time.year}, month{unsigned(time.month)},
                              day{unsigned(time.day)}};
    if (!date.ok())
        throw std::runtime_error("Sun study date is not a Gregorian calendar date");
    const auto utc = sys_days{date} + hours{time.hour} + minutes{time.minute} +
                     seconds{time.second} - minutes{time.utcOffsetMinutes};
    if (utc < sys_days{year{1901} / 1 / 1} || utc >= sys_days{year{2100} / 1 / 1})
        throw std::runtime_error("Resolved sun study UTC date must be within 1901–2099");
    return duration_cast<seconds>(utc.time_since_epoch()).count();
}
void SolarSettings::validate() const {
    if (!std::isfinite(latitude) || latitude < -90 || latitude > 90 || !std::isfinite(longitude) ||
        longitude < -180 || longitude > 180 || !std::isfinite(northDegrees) ||
        northDegrees < -180 || northDegrees > 180)
        throw std::runtime_error(
            "Sun study requires latitude ±90°, longitude ±180° and north rotation ±180°");
    (void)solarUtcSeconds(time);
}
SolarPosition solarPosition(const SolarSettings &settings) {
    settings.validate();
    const auto seconds = solarUtcSeconds(settings.time);
    const double julianDay = 2440587.5 + double(seconds) / 86400;
    const double t = (julianDay - 2451545.) / 36525.;
    const double meanLongitude = wrap(280.46646 + t * (36000.76983 + t * .0003032));
    const double anomaly = wrap(357.52911 + t * (35999.05029 - .0001537 * t));
    const double eccentricity = .016708634 - t * (.000042037 + .0000001267 * t);
    const double center = sinDegrees(anomaly) * (1.914602 - t * (.004817 + .000014 * t)) +
                          sinDegrees(2 * anomaly) * (.019993 - .000101 * t) +
                          sinDegrees(3 * anomaly) * .000289;
    const double omega = 125.04 - 1934.136 * t;
    const double apparentLongitude = meanLongitude + center - .00569 - .00478 * sinDegrees(omega);
    const double obliquity = 23 +
                             (26 + (21.448 - t * (46.815 + t * (.00059 - t * .001813))) / 60) / 60 +
                             .00256 * cosDegrees(omega);
    const double declination = std::asin(sinDegrees(obliquity) * sinDegrees(apparentLongitude));
    const double y = std::pow(std::tan(obliquity * radians / 2), 2);
    const double equation =
        4 / radians *
        (y * sinDegrees(2 * meanLongitude) - 2 * eccentricity * sinDegrees(anomaly) +
         4 * eccentricity * y * sinDegrees(anomaly) * cosDegrees(2 * meanLongitude) -
         .5 * y * y * sinDegrees(4 * meanLongitude) -
         1.25 * eccentricity * eccentricity * sinDegrees(2 * anomaly));
    const double utcMinutes = double((seconds % 86400 + 86400) % 86400) / 60;
    const double hourAngle =
        (wrap((utcMinutes + equation + 4 * settings.longitude) / 4) - 180) * radians;
    const double latitude = settings.latitude * radians;
    const double east = -std::cos(declination) * std::sin(hourAngle);
    const double north = std::cos(latitude) * std::sin(declination) -
                         std::sin(latitude) * std::cos(declination) * std::cos(hourAngle);
    const double up = std::sin(latitude) * std::sin(declination) +
                      std::cos(latitude) * std::cos(declination) * std::cos(hourAngle);
    // Positive north rotation turns geographic north clockwise from model +Y toward +X.
    const double rotation = settings.northDegrees * radians;
    Vec3 direction{east * std::cos(rotation) + north * std::sin(rotation),
                   -east * std::sin(rotation) + north * std::cos(rotation), up};
    direction = direction * (1 / length(direction));
    const bool defined = std::hypot(east, north) > 1e-12;
    return {direction,
            std::asin(std::clamp(direction.z, -1., 1.)) / radians,
            defined ? wrap(std::atan2(east, north) / radians) : 0.,
            declination / radians,
            equation,
            defined,
            direction.z > 0};
}
} // namespace sketchy
