#include "solar/solar.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char *message) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << actual << " != " << expected << '\n';
        throw std::runtime_error(message);
    }
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid solar input accepted");
}
} // namespace
int main() {
    try {
        SolarSettings input;
        input.latitude = 40;
        input.longitude = -105;
        input.time = {2010, 6, 21, 0, 0, 0, -420};
        std::ifstream file(SOLAR_FIXTURE_PATH);
        check(bool(file), "Reference data available");
        std::string line;
        int count{}, crossings{};
        bool previous{};
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream values(line);
            int minute;
            double declination, equation, elevation, azimuth;
            check(bool(values >> minute >> declination >> equation >> elevation >> azimuth),
                  "Reference row parses");
            input.time.hour = minute / 60;
            input.time.minute = minute % 60;
            const auto sun = solarPosition(input);
            near(sun.declinationDegrees, declination, 1e-7, "NOAA reference declination");
            near(sun.equationOfTimeMinutes, equation, 1e-7, "NOAA reference equation of time");
            near(sun.elevationDegrees, elevation, 1e-7, "NOAA geometric elevation");
            near(sun.azimuthDegrees, azimuth, 1e-7, "NOAA geographic azimuth");
            near(length(sun.direction), 1, 1e-14, "Unit direction");
            check(sun.aboveHorizon == (elevation > 0), "Geometric daylight boundary");
            if (count && previous != sun.aboveHorizon)
                ++crossings;
            previous = sun.aboveHorizon;
            ++count;
        }
        check(count == 239 && crossings == 2, "Full day includes both horizon crossings");
        input.time = {2024, 2, 29, 12, 15, 30, 345};
        const auto instant = solarUtcSeconds(input.time);
        auto equivalent = input;
        equivalent.time = {2024, 2, 29, 6, 30, 30, 0};
        check(instant == solarUtcSeconds(equivalent.time),
              "Quarter-hour UTC offset resolves exactly");
        check(solarPosition(input).direction == solarPosition(equivalent).direction,
              "Same UTC instant same sun");
        input.time = {1960, 1, 2, 0, 0, 0, 0};
        equivalent = input;
        equivalent.time = {1960, 1, 1, 19, 0, 0, -300};
        check(solarPosition(input).direction == solarPosition(equivalent).direction,
              "Pre-epoch date rollover");
        auto rotated = input;
        rotated.northDegrees = 90;
        const auto a = solarPosition(input).direction, b = solarPosition(rotated).direction;
        near(b.x, a.y, 1e-14, "North rotation east component");
        near(b.y, -a.x, 1e-14, "North rotation north component");
        near(b.z, a.z, 1e-14, "North rotation preserves elevation");
        input.latitude = 90;
        input.time = {2026, 6, 21, 0, 0, 0, 0};
        for (int hour = 0; hour < 24; ++hour) {
            input.time.hour = hour;
            check(solarPosition(input).aboveHorizon, "Polar summer daylight");
            input.time.month = 12;
            check(!solarPosition(input).aboveHorizon, "Polar winter night");
            input.time.month = 6;
        }
        input.latitude = -90;
        check(!solarPosition(input).aboveHorizon, "Southern pole opposite season");
        input.latitude = 0;
        input.longitude = -180;
        equivalent = input;
        equivalent.longitude = 180;
        near(length(solarPosition(input).direction - solarPosition(equivalent).direction), 0, 1e-14,
             "Dateline equivalence");
        for (auto time :
             {SolarCivilTime{2023, 2, 29}, SolarCivilTime{2024, 4, 31}, SolarCivilTime{1900, 1, 1},
              SolarCivilTime{2100, 1, 1}, SolarCivilTime{2024, 1, 1, 24},
              SolarCivilTime{2024, 1, 1, 0, 0, 60}, SolarCivilTime{2024, 1, 1, 0, 0, 0, 841},
              SolarCivilTime{1901, 1, 1, 0, 0, 0, 60}})
            rejects([&] { solarUtcSeconds(time); });
        for (double invalid : {-91., 91., std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
            auto bad = input;
            bad.latitude = invalid;
            rejects([&] { solarPosition(bad); });
        }
        auto bad = input;
        bad.longitude = 181;
        rejects([&] { solarPosition(bad); });
        bad = input;
        bad.northDegrees = -181;
        rejects([&] { solarPosition(bad); });
        input.time = {1901, 1, 1, 0, 0, 0, 0};
        solarPosition(input);
        input.time = {2099, 12, 31, 23, 59, 59, 0};
        solarPosition(input);
        std::cout << count
                  << " NOAA solar references, UTC offsets, horizon crossings and polar/invalid "
                     "cases passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
