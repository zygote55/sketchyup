#include "app/terrain_report_view.hpp"
#include <QLocale>
#include <QTreeWidget>
#include <cmath>
#include <utility>
namespace sketchy {
namespace {
QString count(double value) { return QLocale().toString(qulonglong(std::llround(value))); }
QString plural(double value, const QString &one, const QString &many) {
    return count(value) + ' ' + (std::llround(value) == 1 ? one : many);
}
QString decimal(double value, int places) { return QLocale().toString(value, 'f', places); }
constexpr std::pair<TerrainReportGroup, const char *> groups[]{
    {TerrainReportGroup::Refusals, "Refusals"},
    {TerrainReportGroup::Merges, "Merges"},
    {TerrainReportGroup::Notices, "Notices"}};
} // namespace

QJsonArray terrainReportKinds(const QJsonObject &terrain, TerrainReportGroup group) {
    QJsonArray kinds;
    if (group == TerrainReportGroup::Refusals)
        return terrain["refusals"].toArray();
    for (const auto &value : terrain["notices"].toArray()) {
        const bool merge = value.toObject()["kind"].toString().startsWith("merged-");
        if (merge == (group == TerrainReportGroup::Merges))
            kinds.append(value);
    }
    return kinds;
}
QString terrainReportKindText(const QJsonObject &kind) {
    auto message = kind["message"].toString();
    if (message.endsWith('.'))
        message.chop(1);
    const auto total = kind["count"].toDouble();
    const auto listed = kind["locations"].toArray().size();
    // The adapter's messages lead with their count; the parser's describe the kind.
    QString text = message.startsWith(count(total) + ' ') ||
                           message.startsWith(QString::number(total) + ' ')
                       ? message
                       : message + " · " + count(total);
    if (listed && kind["locationsOmitted"].toDouble() > 0)
        text += QString(" (%1 listed)").arg(listed);
    return text;
}
QString terrainReportLocationText(const QJsonObject &location, const QString &units) {
    QString text = location["reference"].toString();
    if (location.contains("reference2"))
        text += " and " + location["reference2"].toString();
    const auto xy = location["xy"].toArray();
    if (xy.size() == 2)
        text += QString(" · E %1, N %2 %3")
                    .arg(decimal(xy[0].toDouble(), 3), decimal(xy[1].toDouble(), 3), units);
    if (location.contains("detail"))
        text += " · " + location["detail"].toString();
    return text;
}
QTreeWidgetItem *
fillTerrainReport(QTreeWidget *list, const QJsonObject &terrain, const QString &units,
                  const std::function<std::optional<QPointF>(double, double)> &locate,
                  const QString &chosen) {
    QTreeWidgetItem *restore = nullptr;
    for (const auto &[group, title] : groups) {
        const auto kinds = terrainReportKinds(terrain, group);
        if (kinds.isEmpty())
            continue;
        double total = 0;
        for (const auto &value : kinds)
            total += value.toObject()["count"].toDouble();
        auto *top = new QTreeWidgetItem(
            list, {QString("%1 · %2 %3, %4")
                       .arg(title)
                       .arg(kinds.size())
                       .arg(kinds.size() == 1 ? "kind" : "kinds")
                       .arg(plural(total, "occurrence", "occurrences"))});
        top->setData(0, Qt::UserRole + 2, QString(title).toLower());
        for (const auto &value : kinds) {
            const auto kind = value.toObject();
            auto *item = new QTreeWidgetItem(top, {terrainReportKindText(kind)});
            item->setToolTip(0, terrainReportKindText(kind) + " [" + kind["kind"].toString() + "]");
            item->setData(0, Qt::UserRole + 2, kind["kind"].toString());
            for (const auto &entry : kind["locations"].toArray()) {
                const auto location = entry.toObject();
                auto *child = new QTreeWidgetItem(item, {terrainReportLocationText(location, units)});
                child->setToolTip(0, child->text(0));
                const auto xy = location["xy"].toArray();
                if (xy.size() == 2 && locate)
                    if (const auto point = locate(xy[0].toDouble(), xy[1].toDouble())) {
                        child->setData(0, Qt::UserRole, *point);
                        child->setData(0, Qt::UserRole + 1, true);
                    }
                if (!chosen.isEmpty() && child->text(0) == chosen)
                    restore = child;
            }
            if (const auto omitted = kind["locationsOmitted"].toDouble();
                omitted > 0 && !kind["locations"].toArray().isEmpty())
                new QTreeWidgetItem(item, {"… " + count(omitted) + " more not listed"});
        }
        top->setExpanded(true);
        if (group == TerrainReportGroup::Refusals)
            for (int i = 0; i < top->childCount(); ++i)
                top->child(i)->setExpanded(true);
    }
    return restore;
}
QStringList terrainReportLines(const QJsonObject &terrain, const QString &units) {
    QStringList out;
    for (const auto &[group, title] : groups) {
        const auto kinds = terrainReportKinds(terrain, group);
        if (kinds.isEmpty())
            continue;
        out << "" << title;
        for (const auto &value : kinds) {
            const auto kind = value.toObject();
            out << "  " + terrainReportKindText(kind) + " [" + kind["kind"].toString() + "]";
            for (const auto &location : kind["locations"].toArray())
                out << "    " + terrainReportLocationText(location.toObject(), units);
            if (const auto omitted = kind["locationsOmitted"].toDouble();
                omitted > 0 && !kind["locations"].toArray().isEmpty())
                out << "    … " + count(omitted) + " more not listed";
        }
    }
    return out;
}
} // namespace sketchy
