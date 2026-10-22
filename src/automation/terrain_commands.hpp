#pragma once
// R095.a site and terrain commands and queries (ADR 0168 §5): site.set, site.reanchor
// and terrain.explode are batch commands; site.describe, site.convert,
// terrain.describe and terrain.elevation_at are inspection queries. The in-app assistant
// sees only site.describe, terrain.describe and terrain.elevation_at (R095.g,
// assistantVisible); MCP exposes all of them. R095.e2 adds the batch
// command terrain.set_display (contour lines, interval, shading; the mesh is untouched).
#include "core/model.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_records.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_stamp.hpp"
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
// terrain.remove_edit (R096.d: and terrain.update_edit, which replaces a pad's entry by its
// changed parameters or a Restamp, "Edit terrain change") in three parts, so a desktop worker can replay with progress and cancel
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
    // R096.d terrain.update_edit: the changed entry, which must apply (else the run is refused).
    std::uint32_t required{};
};
TerrainReplayRequest parseTerrainReplayRequest(const Document &doc, const QJsonObject &command);
TerrainImportResult runTerrainReplay(const TerrainReplayRequest &request,
                                     TerrainProgress *progress = nullptr);
QJsonObject applyTerrainReplay(Document &doc, const TerrainReplayRequest &request,
                               TerrainImportResult result, ChangeReport *changes = nullptr);
// ---- R096.f2: the desktop preview of the worker-planned commands (ADR 0169 §5) -------------
// terrain.edit_source, terrain.add_source, terrain.remove_source, terrain.stamp_pad and
// terrain.drape plan on a one-thread worker, latest wins. prepareTerrainEditPreview checks and
// parses `command` on the document exactly as the command does (it throws the command's
// TerrainError or TerrainImportError for a request the command refuses before planning) and keeps
// every input of the plan: the terrain's record, its cached edit mesh, its current sources and the
// document's terrain triangle total. runTerrainEditPreview reads no document and is safe on a
// worker: it runs the command's own dry run on those inputs and builds the candidate tiles
// (replaceTerrainRegion). Committing the same command at the same revision commits these tiles
// bit for bit. A refusal is returned with its located problems, not thrown; a cancellation through
// `progress` gives the code CANCELLED.
struct TerrainEditPreviewRequest;
struct TerrainCommandPreview {
    QJsonObject command;
    QString name; // the command's name
    Id terrain{};
    std::string document; // Document::identity() and revision when prepared
    std::uint64_t revision{};
    Transform world;      // the terrain frame in model coordinates
    std::string kind;     // editSource, addSource, removeSource, stamp or drape
    bool overlay{};       // a drape in overlay mode (no terrain change)
    std::uint32_t edit{}; // the ID the entry would take
    // The candidate tiles (none for an overlay, or when refused).
    std::optional<TerrainEditPreview> candidate;
    // The changed region's outline: the boundary loops of the triangles the change adds (terrain
    // frame), for the hatched band the tools draw around it.
    std::vector<std::vector<Vec3>> outline;
    // Stamps: the triangles the change adds (terrain frame), tinted as pad, cut or fill.
    std::vector<std::array<Vec3, 3>> added;
    // A refusal: the command's code ("INVALID_REQUEST", "LIMIT_EXCEEDED", "CANCELLED", ...) and
    // message, with its located problems (terrain frame). Empty when the change applies.
    std::string code, message;
    std::vector<TerrainEditProblem> problems;
    bool refused() const { return !code.empty(); }
    // For each problem location of kind "crossing-constraints", the current source whose line
    // passes through it (0 when none: a pad's grading ray), in problem and location order.
    std::vector<std::vector<std::uint32_t>> crossed;
    // The current sources the change was planned against (to name sources and pads).
    std::shared_ptr<const TerrainSourceState> sources;
    // Source edits: the source and its geometry as the commit stores it (terrain frame).
    std::uint32_t source{};
    TerrainSourceRole role{TerrainSourceRole::Contour};
    std::vector<Vec3> points;
    // Stamps: the pad with its resolved outline, rays and daylight line.
    std::shared_ptr<const TerrainStampData> pad;
    std::size_t rays{}, cut{}, fill{}, touching{}, dropped{};
    // Drapes: the runs on the surface, their input lines, and the parts outside the outline or
    // in a hole (approximate: sampled along each line), with their lines.
    std::shared_ptr<const TerrainDrapeData> drape;
    std::vector<std::vector<Vec3>> runs;
    std::vector<std::uint32_t> runLines;
    std::vector<std::vector<Vec3>> clippedParts;
    std::vector<std::uint32_t> clippedLines;
    std::size_t clipped{}, faces{}, edgeLines{};
    TerrainDrapeOverlay overlayGeometry; // overlay: the lifted geometry
    double planMs{};                     // the dry run and the candidate, on the worker
};
std::shared_ptr<const TerrainEditPreviewRequest>
prepareTerrainEditPreview(const Document &doc, const QJsonObject &command);
TerrainCommandPreview runTerrainEditPreview(const TerrainEditPreviewRequest &request,
                                            TerrainProgress *progress = nullptr);
// R096.f2 (UX §15.4): the footprint terrain.stamp_pad would read from `footprint` ({body, face},
// {body}, {edges} or {polygon}), in model coordinates: its outline (cleaned, counter-clockwise from
// its smallest point, at its height or at 0 when it has none), its height when it has one, and the
// source body and face. Throws the command's TerrainError for a footprint it refuses.
struct TerrainPadFootprint {
    std::vector<Vec3> outline;
    std::optional<double> z;
    Id body{}, face{};
};
TerrainPadFootprint terrainPadFootprint(const Document &doc, const QJsonValue &footprint);
// The command a request was prepared from.
const QJsonObject &terrainEditPreviewCommand(const TerrainEditPreviewRequest &request);
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
