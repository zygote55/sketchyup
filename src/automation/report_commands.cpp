#include "automation/report_commands.hpp"
#include "automation/inspection.hpp"
#include "core/units.hpp"
#include <QJsonArray>
#include <QString>
#include <cmath>
#include <stdexcept>

namespace sketchy {
namespace {

[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}

// A canonical, non-zero decimal ID string (the API's convention for every entity ID).
Id idFrom(const QJsonValue &value, const char *what) {
    bool ok{};
    const auto text = value.toString();
    const auto id = text.toULongLong(&ok);
    if (!value.isString() || !ok || !id || QString::number(id) != text)
        fail("INVALID_REQUEST", what);
    return id;
}

std::set<Id> idsFrom(const QJsonArray &values, const char *what) {
    std::set<Id> ids;
    for (const auto &value : values)
        if (!ids.insert(idFrom(value, what)).second)
            fail("INVALID_REQUEST", "Report filter IDs must be distinct");
    return ids;
}

// The measure in the display unit (ADR 0175 §3). The factors are those of displayMeasure
// (presentation/unit_display.hpp); the text is built here so it never depends on the locale, and
// the full form is at most eight decimals (displayDecimal's rule) instead of eight significant
// digits.
QString measureText(double metres, int power, DisplayUnit unit, int precision) {
    const auto factor = unit == DisplayUnit::Millimeters  ? 1000.
                        : unit == DisplayUnit::FeetInches ? 1 / .3048
                                                          : 1.;
    auto value = metres * std::pow(factor, power);
    if (precision == fullDisplayPrecision) {
        auto text = QString::number(value, 'f', 8);
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
        return text == "-0" ? QString("0") : text;
    }
    if (std::round(std::abs(value) * std::pow(10., precision)) == 0)
        value = 0; // no negative zero
    return QString::number(value, 'f', precision);
}

// Base unit codes for tabular output (ADR 0175 §3): the compound feet-inches form is display-only.
QString lengthCode(DisplayUnit unit) {
    return unit == DisplayUnit::Meters ? "m" : unit == DisplayUnit::Millimeters ? "mm" : "ft";
}

QString powerCode(DisplayUnit unit, int power) {
    return lengthCode(unit) + (power == 2 ? "2" : "3");
}

QString groupName(ReportGroup group) {
    switch (group) {
    case ReportGroup::None:
        return "none";
    case ReportGroup::Kind:
        return "kind";
    case ReportGroup::Tag:
        return "tag";
    case ReportGroup::Material:
        return "material";
    case ReportGroup::Definition:
        return "definition";
    }
    throw std::runtime_error("Unknown report grouping");
}

QJsonArray idArray(const std::set<Id> &ids) {
    QJsonArray result;
    for (const auto id : ids)
        result.append(QString::number(id));
    return result;
}

} // namespace

ReportRequest reportRequestFrom(const Document &doc, const QJsonObject &request,
                                bool editorAvailable) {
    static const std::set<QString> fields{"metrics", "groupBy", "filters", "precision"};
    for (const auto &field : request.keys())
        if (!fields.contains(field))
            fail("INVALID_REQUEST", "Unknown report request field");

    ReportRequest result;
    if (request.contains("metrics") && request["metrics"].isArray()) {
        result.counts = result.areas = result.volumes = false;
        std::set<QString> seen;
        for (const auto &value : request["metrics"].toArray()) {
            const auto name = value.toString();
            if (!seen.insert(name).second)
                fail("INVALID_REQUEST", "Report metrics must be distinct");
            if (name == "counts")
                result.counts = true;
            else if (name == "areas")
                result.areas = true;
            else if (name == "volumes")
                result.volumes = true;
            else
                fail("INVALID_REQUEST", "Unknown report metric");
        }
        if (seen.empty())
            fail("INVALID_REQUEST", "A report needs at least one metric");
    } else if (request.contains("metrics") && request["metrics"] != "all")
        fail("INVALID_REQUEST", "Report metrics must be all or a list of metrics");

    const auto grouping =
        request.contains("groupBy") ? request["groupBy"].toString("none") : "none";
    if (grouping == "none")
        result.groupBy = ReportGroup::None;
    else if (grouping == "kind")
        result.groupBy = ReportGroup::Kind;
    else if (grouping == "tag")
        result.groupBy = ReportGroup::Tag;
    else if (grouping == "material")
        result.groupBy = ReportGroup::Material;
    else if (grouping == "definition")
        result.groupBy = ReportGroup::Definition;
    else
        fail("INVALID_REQUEST", "Unknown report grouping");

    if (request.contains("filters")) {
        static const std::set<QString> filters{"visibility", "tags", "materials", "definitions",
                                               "selection"};
        const auto object = request["filters"].toObject();
        for (const auto &field : object.keys())
            if (!filters.contains(field))
                fail("INVALID_REQUEST", "Unknown report filter");
        const auto visibility =
            object.contains("visibility") ? object["visibility"].toString("all") : "all";
        if (visibility == "visible")
            result.visibility = ReportVisibility::Visible;
        else if (visibility != "all")
            fail("INVALID_REQUEST", "Report visibility must be all or visible");
        if (object.contains("tags"))
            result.tags = idsFrom(object["tags"].toArray(), "Report tag IDs must be canonical");
        if (object.contains("materials"))
            result.materials =
                idsFrom(object["materials"].toArray(), "Report material IDs must be canonical");
        if (object.contains("definitions"))
            result.definitions =
                idsFrom(object["definitions"].toArray(), "Report definition IDs must be canonical");
        for (const auto tag : result.tags)
            if (!doc.tags().contains(tag))
                fail("NOT_FOUND", "Report tag does not exist");
        for (const auto material : result.materials)
            if (!doc.materials().contains(material))
                fail("NOT_FOUND", "Report material does not exist");
        for (const auto definition : result.definitions)
            if (!doc.definitions().contains(definition))
                fail("NOT_FOUND", "Component definition does not exist");
        if (object.contains("selection")) {
            if (!object["selection"].isBool())
                fail("INVALID_REQUEST", "Report selection must be a boolean");
            result.selection = object["selection"].toBool();
            if (result.selection && !editorAvailable)
                fail("INVALID_REQUEST",
                     "selection requires explicit editor state; unavailable in file-only queries");
        }
    }

    if (request.contains("precision")) {
        const auto precision = request["precision"];
        if (precision == "full")
            result.precision = fullDisplayPrecision;
        else if (precision.isDouble() && precision.toDouble() == std::floor(precision.toDouble())) {
            const auto unit = doc.displayUnits();
            if (!validDisplayPrecision(unit, precision.toInt()) || precision.toInt() < 0)
                fail("LIMIT_EXCEEDED", (std::string("Report precision must be full or 0-") +
                                        std::to_string(maxDisplayPrecision(unit)) + " for " +
                                        std::string(unitCode(unit)))
                                           .c_str());
            result.precision = precision.toInt();
        } else
            fail("INVALID_REQUEST", "Report precision must be full or an integer");
    }
    return result;
}

QJsonObject reportDescription(const Document &doc, const ReportRequest &request,
                              const std::set<Id> *selectedBodies) {
    ReportResult result;
    try {
        result = runReport(doc, request, selectedBodies);
    } catch (const std::invalid_argument &error) {
        fail("INVALID_REQUEST", error.what());
    }
    const auto unit = doc.displayUnits();
    const auto precision = result.precision;

    QJsonArray metrics;
    if (result.request.counts)
        metrics.append("counts");
    if (result.request.areas)
        metrics.append("areas");
    if (result.request.volumes)
        metrics.append("volumes");

    QJsonObject filters{
        {"visibility", result.request.visibility == ReportVisibility::Visible ? "visible" : "all"},
        {"tags", idArray(result.request.tags)},
        {"materials", idArray(result.request.materials)},
        {"definitions", idArray(result.request.definitions)},
        {"selection", result.request.selection}};
    QJsonObject metadata{{"documentId", QString::fromStdString(result.documentId)},
                         {"revision", QString::number(result.revision)},
                         {"metrics", metrics},
                         {"groupBy", groupName(result.request.groupBy)},
                         {"units", QJsonObject{{"length", lengthCode(unit)},
                                               {"area", powerCode(unit, 2)},
                                               {"volume", powerCode(unit, 3)}}},
                         {"documentUnits", QString::fromLatin1(unitCode(unit).data())},
                         {"precision", precision == fullDisplayPrecision ? QJsonValue("full")
                                                                         : QJsonValue(precision)},
                         {"filters", filters},
                         {"sheets", int(result.sheets)},
                         {"views", int(result.views)},
                         {"items", int(result.items)}};
    if (!result.request.materials.empty())
        metadata["materialsApply"] = "areas";

    QJsonArray counts, areas, volumes, invalid, terrain;
    for (const auto &row : result.counts)
        counts.append(QJsonObject{{"key", QString::fromStdString(row.key)},
                                  {"contexts", int(row.contexts)},
                                  {"instances", int(row.instances)},
                                  {"definitions", int(row.definitions)}});
    for (const auto &row : result.areas)
        areas.append(QJsonObject{
            {"key", QString::fromStdString(row.key)},
            {"material", row.material ? QString::number(row.material) : QString::fromUtf8("—")},
            {"area", measureText(row.area, 2, unit, precision)}});
    for (const auto &row : result.volumes)
        volumes.append(QJsonObject{{"key", QString::fromStdString(row.key)},
                                   {"volume", measureText(row.volume, 3, unit, precision)},
                                   {"unvalidated", int(row.unvalidated)}});
    for (const auto &row : result.invalid)
        invalid.append(QJsonObject{{"status", QString::fromStdString(row.status)},
                                   {"contexts", int(row.contexts)}});
    for (const auto &row : result.terrain)
        terrain.append(QJsonObject{{"terrain", QString::number(row.terrain)},
                                   {"contexts", int(row.contexts)},
                                   {"triangles", int(row.triangles)},
                                   {"area", measureText(row.area, 2, unit, precision)}});

    QJsonObject data{{"metadata", metadata}, {"terrain", terrain}};
    if (result.request.counts)
        data["counts"] = counts;
    if (result.request.areas)
        data["areas"] = areas;
    if (result.request.volumes) {
        data["volumes"] = volumes;
        data["invalid"] = invalid;
    }
    return data;
}

} // namespace sketchy
