#include "io/report_csv.hpp"
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace sketchy {
namespace {

// Minimal RFC 4180 quoting: a field is quoted only when it holds a comma, a double quote, a CR or
// an LF, and its double quotes are doubled.
QString csvField(const QString &text) {
    const QLatin1Char comma(','), quote('"'), cr('\r'), lf('\n');
    if (!text.contains(comma) && !text.contains(quote) && !text.contains(cr) && !text.contains(lf))
        return text;
    QString quoted = text;
    quoted.replace(quote, QStringLiteral("\"\""));
    return QStringLiteral("\"") + quoted + QStringLiteral("\"");
}

// A metadata scalar as written: the precision is `full` or an integer, the others are strings.
QString scalar(const QJsonValue &value) {
    return value.isString() ? value.toString() : QString::number(value.toInt());
}

QString joinedStrings(const QJsonArray &values, QChar separator) {
    QStringList items;
    for (const auto &value : values)
        items.append(value.toString());
    return items.join(separator);
}

// An ID cell: the decimal ID, or the dash of a group without one (untagged, no material, unplaced).
bool isId(const QString &key) {
    if (key.isEmpty())
        return false;
    for (const auto character : key)
        if (!character.isDigit())
            return false;
    return true;
}

// The tag, material or definition cell of a group row. It is set only for the axis the report
// groups by, and never on the All row.
QString axisCell(const QString &groupBy, const QString &axis, const QJsonObject &row) {
    const auto key = row["key"].toString();
    if (groupBy != axis || key == QStringLiteral("All"))
        return {};
    return isId(key) ? key : QStringLiteral("—");
}

} // namespace

QByteArray reportCsv(const QJsonObject &data) {
    const auto metadata = data["metadata"].toObject();
    const auto units = metadata["units"].toObject();
    const auto filters = metadata["filters"].toObject();
    const auto groupBy = metadata["groupBy"].toString();

    QString text;
    const auto line = [&](const QString &value) { text += value + QLatin1Char('\n'); };
    // The metadata block, in the ADR's fixed order (§6.1). Metric and ID lists are semicolon-joined
    // so that no metadata line contains a comma.
    line(QStringLiteral("#sketchyup-report=1"));
    line(QStringLiteral("#format=1.0"));
    line(QStringLiteral("#documentId=") + metadata["documentId"].toString());
    line(QStringLiteral("#revision=") + metadata["revision"].toString());
    line(QStringLiteral("#ad-hoc=1"));
    line(QStringLiteral("#units.length=") + units["length"].toString());
    line(QStringLiteral("#units.area=") + units["area"].toString());
    line(QStringLiteral("#units.volume=") + units["volume"].toString());
    line(QStringLiteral("#precision=") + scalar(metadata["precision"]));
    line(QStringLiteral("#metrics=") +
         joinedStrings(metadata["metrics"].toArray(), QLatin1Char(';')));
    line(QStringLiteral("#group_by=") + groupBy);
    line(QStringLiteral("#filters.visibility=") + filters["visibility"].toString());
    const std::pair<const char *, const char *> idFilters[]{
        {"tags", "#filters.tags="},
        {"materials", "#filters.materials="},
        {"definitions", "#filters.definitions="}};
    for (const auto &[field, prefix] : idFilters) {
        const auto ids = filters[field].toArray();
        if (!ids.isEmpty())
            line(QString::fromLatin1(prefix) + joinedStrings(ids, QLatin1Char(';')));
    }
    if (filters["selection"].toBool())
        line(QStringLiteral("#filters.selection=1"));
    line(QStringLiteral("#sheets=") + QString::number(metadata["sheets"].toInt()));
    line(QStringLiteral("#views=") + QString::number(metadata["views"].toInt()));
    line(QStringLiteral("#items=") + QString::number(metadata["items"].toInt()));

    line(QStringLiteral("section,group,tag,material,definition,metric,value"));
    const auto row = [&](const char *section, const QString &group, const QString &tag,
                         const QString &material, const QString &definition, const char *metric,
                         const QString &value) {
        const QStringList cells{QString::fromLatin1(section), group, tag, material, definition,
                                QString::fromLatin1(metric),  value};
        QStringList quoted;
        for (const auto &cell : cells)
            quoted.append(csvField(cell));
        line(quoted.join(QLatin1Char(',')));
    };

    for (const auto &value : data["counts"].toArray()) {
        const auto group = value.toObject();
        const auto name = group["name"].toString();
        const auto tag = axisCell(groupBy, QStringLiteral("tag"), group);
        const auto material = axisCell(groupBy, QStringLiteral("material"), group);
        const auto definition = axisCell(groupBy, QStringLiteral("definition"), group);
        row("counts", name, tag, material, definition, "contexts",
            QString::number(group["contexts"].toInt()));
        row("counts", name, tag, material, definition, "instances",
            QString::number(group["instances"].toInt()));
        row("counts", name, tag, material, definition, "definitions",
            QString::number(group["definitions"].toInt()));
    }
    for (const auto &value : data["areas"].toArray()) {
        const auto group = value.toObject();
        row("areas", group["name"].toString(), axisCell(groupBy, QStringLiteral("tag"), group),
            group["material"].toString(), axisCell(groupBy, QStringLiteral("definition"), group),
            "area", group["area"].toString());
    }
    for (const auto &value : data["volumes"].toArray()) {
        const auto group = value.toObject();
        const auto name = group["name"].toString();
        const auto tag = axisCell(groupBy, QStringLiteral("tag"), group);
        const auto material = axisCell(groupBy, QStringLiteral("material"), group);
        const auto definition = axisCell(groupBy, QStringLiteral("definition"), group);
        row("volumes", name, tag, material, definition, "volume", group["volume"].toString());
        row("volumes", name, tag, material, definition, "unvalidated",
            QString::number(group["unvalidated"].toInt()));
    }
    for (const auto &value : data["invalid"].toArray()) {
        const auto status = value.toObject();
        row("invalid", status["status"].toString(), {}, {}, {}, "contexts",
            QString::number(status["contexts"].toInt()));
    }
    for (const auto &value : data["terrain"].toArray()) {
        const auto terrain = value.toObject();
        const auto name = terrain["name"].toString();
        row("terrain", name, {}, {}, {}, "contexts", QString::number(terrain["contexts"].toInt()));
        row("terrain", name, {}, {}, {}, "triangles",
            QString::number(terrain["triangles"].toInt()));
        row("terrain", name, {}, {}, {}, "area", terrain["area"].toString());
    }
    return text.toUtf8();
}

} // namespace sketchy
