#include "io/text_source_io.hpp"
#include <QJsonArray>
#include <QStringList>
#include <cmath>
#include <stdexcept>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void fields(const QJsonObject &o, const QStringList &keys) {
    require(o.size() == keys.size(), "Text source has missing or unknown fields");
    for (const auto &key : keys)
        require(o.contains(key), "Text source is missing a field");
}
std::string text(const QJsonValue &value) {
    require(value.isString(), "Text source requires a string");
    const auto s = value.toString();
    require(s.isValidUtf16(), "Text source requires valid Unicode");
    return s.toUtf8().toStdString();
}
double number(const QJsonValue &value) {
    require(value.isDouble() && std::isfinite(value.toDouble()), "Text source requires a number");
    return value.toDouble();
}
size_t count(const QJsonValue &value) {
    const auto n = number(value);
    require(n >= 0 && n <= 1024 && n == std::floor(n), "Invalid text source count");
    return size_t(n);
}
bool flag(const QJsonValue &value) {
    require(value.isBool(), "Text source requires a boolean");
    return value.toBool();
}
} // namespace
QJsonObject encodeTextSource(const TextSource &s) {
    validateTextSource(s);
    QJsonArray fonts;
    for (const auto &font : s.fonts)
        fonts.append(QJsonObject{{"family", QString::fromStdString(font.family)},
                                 {"style", QString::fromStdString(font.style)},
                                 {"fingerprint", QString::fromStdString(font.fingerprint)},
                                 {"glyphs", int(font.glyphs)}});
    return {{"version", 1},
            {"text", QString::fromStdString(s.text)},
            {"family", QString::fromStdString(s.family)},
            {"style", QString::fromStdString(s.style)},
            {"height", s.height},
            {"depth", s.depth},
            {"lineSpacing", s.lineSpacing},
            {"allowSubstitution", s.allowSubstitution},
            {"actualFamily", QString::fromStdString(s.actualFamily)},
            {"actualStyle", QString::fromStdString(s.actualStyle)},
            {"substituted", s.substituted},
            {"fallback", s.fallback},
            {"fonts", fonts},
            {"geometryDigest", QString::fromStdString(s.geometryDigest)},
            {"regions", int(s.regions)}};
}
TextSource decodeTextSource(const QJsonObject &o) {
    fields(o, {"version", "text", "family", "style", "height", "depth", "lineSpacing",
               "allowSubstitution", "actualFamily", "actualStyle", "substituted", "fallback",
               "fonts", "geometryDigest", "regions"});
    require(o["version"] == 1, "Unsupported text source version");
    TextSource s;
    s.text = text(o["text"]);
    s.family = text(o["family"]);
    s.style = text(o["style"]);
    s.height = number(o["height"]);
    s.depth = number(o["depth"]);
    s.lineSpacing = number(o["lineSpacing"]);
    s.allowSubstitution = flag(o["allowSubstitution"]);
    s.actualFamily = text(o["actualFamily"]);
    s.actualStyle = text(o["actualStyle"]);
    s.substituted = flag(o["substituted"]);
    s.fallback = flag(o["fallback"]);
    s.geometryDigest = text(o["geometryDigest"]);
    s.regions = count(o["regions"]);
    require(o["fonts"].isArray() && o["fonts"].toArray().size() <= 64, "Invalid text font table");
    for (const auto value : o["fonts"].toArray()) {
        require(value.isObject(), "Invalid text font record");
        const auto f = value.toObject();
        fields(f, {"family", "style", "fingerprint", "glyphs"});
        s.fonts.push_back(
            {text(f["family"]), text(f["style"]), text(f["fingerprint"]), count(f["glyphs"])});
    }
    validateTextSource(s);
    return s;
}
} // namespace sketchy
