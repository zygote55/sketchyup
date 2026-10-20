#pragma once
// R095.a site and terrain commands and queries (ADR 0168 §5): site.set, site.reanchor
// and terrain.explode are batch commands; site.describe, site.convert,
// terrain.describe and terrain.elevation_at are inspection queries. The in-app assistant
// sees only site.describe, terrain.describe and terrain.elevation_at (R095.g,
// assistantVisible); MCP exposes all of them. R095.e2 adds the batch
// command terrain.set_display (contour lines, interval, shading; the mesh is untouched).
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
// D-044: `progress` reports parse, prepare, triangulate and tile (the CLI's stage progress).
// A cancelled run throws TerrainImportError CANCELLED and changes nothing.
QJsonObject importTerrainSource(Document &doc, const QByteArray &bytes, const QJsonObject &command,
                                TerrainProgress *progress = nullptr);
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
// D-044: with `progress`, parsing reports TerrainStage::Parse and a cancelled parse throws
// TerrainCancelled (not a TerrainError); pass the same TerrainProgress on to runTerrainImport.
TerrainSourceRequest parseTerrainSourceRequest(const QByteArray &bytes, const QJsonObject &command,
                                               bool importing = true,
                                               TerrainProgress *progress = nullptr);
QJsonObject applyTerrainImport(Document &doc, TerrainImportResult result,
                               const std::string &name = "Terrain");
// R095.h: terrain.create_from_edges in two halves, as terrain.import above, so Create terrain
// from selection's roles step checks the chosen roles with the command's own validation and
// report. parseTerrainEdgeRequest checks the request and chains the edges on the document
// (throwing TerrainError exactly as the command); runTerrainEdgeRequest reads no document (safe
// on a worker) and gives the result and bounded report the command would: on a refusal
// `ok` is false and `code`, `message` ("Terrain creation refused …") and `report` say why.
struct TerrainEdgeRequest {
    TerrainSourceSet set;
    TerrainImportOptions options;
    std::string name{"Terrain"};
};
TerrainEdgeRequest parseTerrainEdgeRequest(const Document &doc, const QJsonObject &command);
// D-044: with `progress`, as runTerrainImport: a cancelled run returns `cancelled`.
TerrainImportResult runTerrainEdgeRequest(const TerrainEdgeRequest &request,
                                          TerrainProgress *progress = nullptr);
// The terrain.create_from_edges fields for an edge selection: `edges` (always present),
// `roles.contour` / `roles.breakline`, `boundary` and `holes` when non-empty.
QJsonObject terrainCreateFromEdgesCommand(const TerrainEdgeSelection &selection);
// R096.a: terrain.regenerate, terrain.set_parameters, terrain.reset_edits and
// terrain.remove_edit in three parts, so a desktop worker can replay with progress and cancel
// (ADR 0169 §2). parseTerrainReplayRequest checks the command on the document (throwing
// TerrainError exactly as the command: NOT_FOUND for a missing terrain or change,
// INVALID_REQUEST for no edits to reset, INVALID_OPERATION when locked or, with edits, not
// level) and builds the record to replay; runTerrainReplay reads no document (the CDT of the
// base sources and the replay); applyTerrainReplay applies it as ONE labelled edit, refusing a
// failed or cancelled run (TerrainImportError, "terrainReport") and a terrain whose record
// changed since the request was made. Reset edits empties the list and Remove change removes one
// entry; both then follow Regenerate's rules (tile IDs, paint notices).
struct TerrainReplayRequest {
    QString command;
    std::string label;
    Id terrain{};
    TerrainRecord current; // the record when the request was made
    TerrainRecord record;  // the record to replay (its edit list changed by the command)
    TerrainParameters parameters;
    std::size_t otherTriangles{};
    TerrainReport notices;
};
TerrainReplayRequest parseTerrainReplayRequest(const Document &doc, const QJsonObject &command);
TerrainImportResult runTerrainReplay(const TerrainReplayRequest &request,
                                     TerrainProgress *progress = nullptr);
QJsonObject applyTerrainReplay(Document &doc, const TerrainReplayRequest &request,
                               TerrainImportResult result, ChangeReport *changes = nullptr);
// terrain.describe's `edits`: count, bytes and at most 200 entries {id, kind, bounds} (bounds in
// the terrain frame, when the kind gives them), with the rest counted.
QJsonObject terrainEditsJson(const TerrainRecord &record);
// Entries in the shared command catalogue format.
QJsonArray terrainCommandSpecs();
QJsonObject siteDescription(const Document &doc);
// "site" converts model points to site coordinates, "model" the reverse.
QJsonObject siteConversion(const Document &doc, const QString &to, const QJsonArray &points);
QJsonObject terrainDescription(const Document &doc, Id terrain);
// R095.e2: terrain.set_display's settings as terrain.describe reports them.
QJsonObject terrainDisplayJson(const TerrainDisplay &display);
QJsonObject terrainElevationDescription(const Document &doc, Id terrain, const QJsonArray &points);
} // namespace sketchy
