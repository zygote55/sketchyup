#pragma once
// R095.a site and terrain commands and queries (ADR 0168 §5): site.set, site.reanchor
// and terrain.explode are batch commands; site.describe, site.convert,
// terrain.describe and terrain.elevation_at are inspection queries. None is shown to
// the in-app assistant yet (assistantVisible); MCP exposes all of them.
#include "core/model.hpp"
#include "core/terrain_records.hpp"
#include "io/terrain_import.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
// R095.c: terrain.import is a batch command; terrain.inspect_source parses and dry-runs a
// source without changing anything (a batch holding only that command returns
// {"status": "inspected"} at the unchanged revision).
// Largest inline source: `data` is base64 of at most 16 MiB (22,369,624 characters, the
// asset.import bound). Session, MCP and transaction requests stay within their 64 KiB request
// bound, so through them a source is at most about 47 KiB; the in-process path (desktop) and
// the CLI file path take the full 64 MiB source budget.
inline constexpr int terrainDataCharacters = 22369624;
bool isTerrainCommand(const QString &name);
// A terrain.import refusal: the shared code plus the bounded import report. R095.c2's
// commands (create_from_edges, set_parameters, regenerate) refuse with the same report shape
// under the key "terrainReport".
class TerrainImportError : public TerrainError {
  public:
    TerrainImportError(std::string code, const std::string &message, QJsonObject report,
                       QString key = "importReport")
        : TerrainError(std::move(code), message), report_(std::move(report)), key_(std::move(key)) {
    }
    const QJsonObject &report() const { return report_; }
    const QString &reportKey() const { return key_; }

  private:
    QJsonObject report_;
    QString key_;
};
// `operation`, when given, receives the command's result record (terrain.import: the new
// terrain, its tiles and the import report; R095.c2 terrain.create_from_edges,
// terrain.set_parameters and terrain.regenerate: the terrain, its tiles, whether anything
// changed and the bounded "terrainReport"). Regenerating a terrain that is already current
// changes nothing and records no edit (the batch then has no committed changes).
ChangeReport executeTerrainCommand(Document &doc, const QJsonObject &command,
                                   QJsonObject *operation = nullptr);
// terrain.inspect_source with base64 `data`.
QJsonObject inspectTerrainCommand(const Document &doc, const QJsonObject &command);
// The same two operations on source bytes, with the other command fields as options (the CLI
// reads a file instead of `data`; `data` is ignored here). importTerrainSource applies one
// labelled edit and returns the operation record; a refusal throws TerrainImportError and
// changes nothing.
QJsonObject inspectTerrainSource(const Document &doc, const QByteArray &bytes,
                                 const QJsonObject &command);
QJsonObject importTerrainSource(Document &doc, const QByteArray &bytes,
                                const QJsonObject &command);
// R095.d: importTerrainSource in its two halves, so the desktop dialog parses and dry-runs a
// source off the UI thread and applies only the edit on it. parseTerrainSourceRequest checks
// and parses exactly as the commands do (throwing TerrainError); runTerrainImport then gives
// the dry run, and applyTerrainImport applies a successful one as the ONE labelled "Import
// terrain" edit (refusing a failed one with TerrainImportError, changing nothing). The dry run
// must have been made with the document's current site origin.
struct TerrainSourceRequest {
    TerrainSourceSet set;
    TerrainImportOptions options;
    TerrainImportProvenance provenance;
    std::string name{"Terrain"};
};
TerrainSourceRequest parseTerrainSourceRequest(const QByteArray &bytes, const QJsonObject &command,
                                               bool importing = true);
QJsonObject applyTerrainImport(Document &doc, TerrainImportResult result,
                               const std::string &name = "Terrain");
// Entries in the shared command catalogue format.
QJsonArray terrainCommandSpecs();
QJsonObject siteDescription(const Document &doc);
// "site" converts model points to site coordinates, "model" the reverse.
QJsonObject siteConversion(const Document &doc, const QString &to, const QJsonArray &points);
QJsonObject terrainDescription(const Document &doc, Id terrain);
QJsonObject terrainElevationDescription(const Document &doc, Id terrain, const QJsonArray &points);
} // namespace sketchy
