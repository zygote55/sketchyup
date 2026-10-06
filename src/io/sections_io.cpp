#include "io/sections_io.hpp"
#include <QJsonObject>
#include <QStringList>
namespace sketchy {
namespace {
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    if (!value.isObject())
        throw std::runtime_error("Section requires an object");
    const auto result = value.toObject();
    if (result.size() != fields.size())
        throw std::runtime_error("Section object has missing or unknown fields");
    for (const auto &field : fields)
        if (!result.contains(field))
            throw std::runtime_error("Missing section field");
    return result;
}
Id id(const QJsonValue &value, bool zero = false) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || (!zero && !result) || QString::number(result) != text)
        throw std::runtime_error("Section identity requires a canonical decimal string");
    return result;
}
QJsonArray array(const QJsonValue &value, qsizetype maximum) {
    if (!value.isArray() || value.toArray().size() > maximum)
        throw std::runtime_error("Section array exceeds shape or record budget");
    return value.toArray();
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Section requires finite numeric coefficients");
    return value.toDouble();
}
bool flag(const QJsonValue &value) {
    if (!value.isBool())
        throw std::runtime_error("Section requires boolean display flags");
    return value.toBool();
}
} // namespace
QJsonArray encodeSections(const SectionRecords &sections) {
    QJsonArray result;
    for (const auto &[key, record] : sections) {
        const auto &p = record->plane;
        result.append(QJsonObject{
            {"id", QString::number(key)},
            {"name", QString::fromStdString(record->name)},
            {"context", QString::number(record->context)},
            {"plane", QJsonArray{p.normal.x, p.normal.y, p.normal.z, p.offset}},
            {"fill", record->fill},
            {"edges", record->edges},
            {"color", QJsonArray{record->color[0], record->color[1], record->color[2]}}});
    }
    return result;
}
SectionRecords decodeSections(const QJsonValue &value, Id nextSectionId) {
    SectionRecords result;
    for (const auto &entry : array(value, sectionRecordLimit)) {
        const auto row =
            object(entry, {"id", "name", "context", "plane", "fill", "edges", "color"});
        if (!row["name"].isString())
            throw std::runtime_error("Section name requires text");
        const auto plane = array(row["plane"], 4), color = array(row["color"], 3);
        if (plane.size() != 4 || color.size() != 3)
            throw std::runtime_error("Section requires four plane coefficients and RGB color");
        SectionRecord record;
        record.id = id(row["id"]);
        record.name = row["name"].toString().toStdString();
        record.context = id(row["context"], true);
        record.plane = {{number(plane[0]), number(plane[1]), number(plane[2])}, number(plane[3])};
        record.fill = flag(row["fill"]);
        record.edges = flag(row["edges"]);
        for (size_t i = 0; i < 3; ++i) {
            const auto channel = number(color[qsizetype(i)]);
            if (channel < 0 || channel > 1)
                throw std::runtime_error("Section fill color outside [0,1]");
            record.color[i] = float(channel);
        }
        if (!result.emplace(record.id, std::make_shared<SectionRecord>(record)).second)
            throw std::runtime_error("Repeated section plane identity");
    }
    validateSectionRecords(result, nextSectionId, {});
    return result;
}
QJsonArray encodeActiveSections(const ActiveSections &active) {
    QJsonArray result;
    for (const auto &[context, section] : active)
        result.append(QJsonObject{{"context", QString::number(context)},
                                  {"section", QString::number(section)}});
    return result;
}
ActiveSections decodeActiveSections(const QJsonValue &value) {
    ActiveSections result;
    for (const auto &entry : array(value, sectionRecordLimit)) {
        const auto row = object(entry, {"context", "section"});
        if (!result.emplace(id(row["context"], true), id(row["section"])).second)
            throw std::runtime_error("Repeated active section context");
    }
    return result;
}
} // namespace sketchy
