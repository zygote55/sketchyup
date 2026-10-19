#pragma once
// The bounded terrain report's presentation (ADR 0168 "Decisions > Interaction design",
// Report), shared by File > Import terrain… (R095.d) and the R095.e1 terrain dialogs (Terrain
// settings, Regenerate and Create terrain from selection): refusals, merges and notices grouped
// by kind, each with its complete count and at most 32 locations.
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
class QTreeWidget;
class QTreeWidgetItem;
namespace sketchy {
enum class TerrainReportGroup { Refusals, Merges, Notices };
// Refusals; merges (notices whose kind starts "merged-"); the other notices.
QJsonArray terrainReportKinds(const QJsonObject &terrain, TerrainReportGroup group);
// "<message> · <count>" ("(N listed)" when locations were truncated).
QString terrainReportKindText(const QJsonObject &kind);
// "<reference> and <reference2> · E x, N y <units> · <detail>".
QString terrainReportLocationText(const QJsonObject &location, const QString &units);
// Fills `list` with one top-level item per non-empty group, its kinds and their locations. A
// location whose XY `locate` maps to a point stores it (Qt::UserRole, with UserRole + 1 true);
// each group and kind item stores its key in UserRole + 2. Returns the location item whose text
// equals `chosen`, if any.
QTreeWidgetItem *fillTerrainReport(
    QTreeWidget *list, const QJsonObject &terrain, const QString &units,
    const std::function<std::optional<QPointF>(double, double)> &locate = {},
    const QString &chosen = {});
// The groups as text lines for Copy report: a title line per group, an indented line per kind
// and a further indented line per location.
QStringList terrainReportLines(const QJsonObject &terrain, const QString &units);
} // namespace sketchy
