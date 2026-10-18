#pragma once
// R095.a site and terrain commands and queries (ADR 0168 §5): site.set, site.reanchor
// and terrain.explode are batch commands; site.describe, site.convert,
// terrain.describe and terrain.elevation_at are inspection queries. None is shown to
// the in-app assistant yet (assistantVisible); MCP exposes all of them.
#include "core/model.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
bool isTerrainCommand(const QString &name);
ChangeReport executeTerrainCommand(Document &doc, const QJsonObject &command);
// Entries in the shared command catalogue format.
QJsonArray terrainCommandSpecs();
QJsonObject siteDescription(const Document &doc);
// "site" converts model points to site coordinates, "model" the reverse.
QJsonObject siteConversion(const Document &doc, const QString &to, const QJsonArray &points);
QJsonObject terrainDescription(const Document &doc, Id terrain);
QJsonObject terrainElevationDescription(const Document &doc, Id terrain, const QJsonArray &points);
} // namespace sketchy
