#include "io/solar_io.hpp"
#include <QJsonArray>
#include <QStringList>
#include <limits>
namespace sketchy {
QJsonObject encodeSolarSettings(const SolarSettings &s) {
    s.validate();
    return {{"algorithm", QString::fromLatin1(solarAlgorithm.data())},
            {"enabled", s.enabled},
            {"shadows", s.shadows},
            {"latitude", s.latitude},
            {"longitude", s.longitude},
            {"northDegrees", s.northDegrees},
            {"year", s.time.year},
            {"month", s.time.month},
            {"day", s.time.day},
            {"hour", s.time.hour},
            {"minute", s.time.minute},
            {"second", s.time.second},
            {"utcOffsetMinutes", s.time.utcOffsetMinutes}};
}
SolarSettings decodeSolarSettings(const QJsonValue &value) {
    const QStringList names{"algorithm",    "enabled", "shadows",         "latitude", "longitude",
                            "northDegrees", "year",    "month",           "day",      "hour",
                            "minute",       "second",  "utcOffsetMinutes"};
    if (!value.isObject() || value.toObject().size() != names.size())
        throw std::runtime_error("Sun study requires exactly its published fields");
    const auto object = value.toObject();
    for (const auto &name : names)
        if (!object.contains(name))
            throw std::runtime_error("Missing sun study field");
    if (object["algorithm"] != QString::fromLatin1(solarAlgorithm.data()))
        throw std::runtime_error("Unsupported solar position algorithm");
    auto number = [&](const char *name) {
        const auto v = object[name];
        if (!v.isDouble() || !std::isfinite(v.toDouble()))
            throw std::runtime_error("Sun study requires finite numbers");
        return v.toDouble();
    };
    auto integer = [&](const char *name) {
        const auto v = number(name);
        if (v != std::floor(v) || v < std::numeric_limits<int>::min() ||
            v > std::numeric_limits<int>::max())
            throw std::runtime_error("Sun study date/time requires integers");
        return int(v);
    };
    auto flag = [&](const char *name) {
        if (!object[name].isBool())
            throw std::runtime_error("Sun study flags require booleans");
        return object[name].toBool();
    };
    SolarSettings s;
    s.enabled = flag("enabled");
    s.shadows = flag("shadows");
    s.latitude = number("latitude");
    s.longitude = number("longitude");
    s.northDegrees = number("northDegrees");
    s.time = {integer("year"),
              integer("month"),
              integer("day"),
              integer("hour"),
              integer("minute"),
              integer("second"),
              integer("utcOffsetMinutes")};
    s.validate();
    return s;
}
QJsonObject describeSolarPosition(const SolarSettings &settings) {
    const auto sun = solarPosition(settings);
    return {{"algorithm", QString::fromLatin1(solarAlgorithm.data())},
            {"utcSeconds", QString::number(solarUtcSeconds(settings.time))},
            {"direction", QJsonArray{sun.direction.x, sun.direction.y, sun.direction.z}},
            {"elevationDegrees", sun.elevationDegrees},
            {"azimuthDegrees",
             sun.azimuthDefined ? QJsonValue(sun.azimuthDegrees) : QJsonValue(QJsonValue::Null)},
            {"declinationDegrees", sun.declinationDegrees},
            {"equationOfTimeMinutes", sun.equationOfTimeMinutes},
            {"aboveHorizon", sun.aboveHorizon},
            {"directLightActive", settings.enabled && sun.aboveHorizon},
            {"shadowsActive", settings.enabled && settings.shadows && sun.aboveHorizon},
            {"refraction", "none; geometric solar center"}};
}
} // namespace sketchy
