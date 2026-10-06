#include "automation/solar_commands.hpp"
#include "io/solar_io.hpp"
#include <QJsonArray>
namespace sketchy {
QJsonObject solarSettingsSchema() {
    auto range = [](double low, double high, bool integer = false) {
        return QJsonObject{
            {"type", integer ? "integer" : "number"}, {"minimum", low}, {"maximum", high}};
    };
    const QJsonObject flag{{"type", "boolean"}};
    const QJsonObject properties{
        {"algorithm",
         QJsonObject{{"type", "string"}, {"const", QString::fromLatin1(solarAlgorithm.data())}}},
        {"enabled", flag},
        {"shadows", flag},
        {"latitude", range(-90, 90)},
        {"longitude", range(-180, 180)},
        {"northDegrees", range(-180, 180)},
        {"year", range(1901, 2099, true)},
        {"month", range(1, 12, true)},
        {"day", range(1, 31, true)},
        {"hour", range(0, 23, true)},
        {"minute", range(0, 59, true)},
        {"second", range(0, 59, true)},
        {"utcOffsetMinutes", range(-840, 840, true)}};
    QJsonArray required;
    for (const auto &name : properties.keys())
        required.append(name);
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", false},
            {"description",
             "Complete offline sun study. UTC offset is explicit and includes any user-selected "
             "daylight-saving adjustment; invalid calendar dates reject."}};
}
QJsonObject solarDescription(const Document &document) {
    return {
        {"settings", encodeSolarSettings(document.solar())},
        {"position", describeSolarPosition(document.solar())},
        {"timeZonePolicy", "Explicit fixed UTC offset; no OS zone or daylight-saving inference"}};
}
} // namespace sketchy
