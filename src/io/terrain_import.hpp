#pragma once
// R095.c terrain import preparation (ADR 0168 sections 1, 3 and 4): a parsed source set in
// source units becomes canonical terrain source records in the local frame, the records
// become the adapter input, and the adapter's report is mapped back to source references
// and source units.
//
//   source set --prepareTerrainSources--> TerrainSource records (local metres, canonical)
//              --terrainTriangulationInput--> TerrainTriangulationInput --triangulateTerrain-->
//
// The middle step reads only stored records, so Regenerate and terrain.set_parameters
// (R095.c2) rebuild the same input from a TerrainRecord.
#include "core/terrain.hpp"
#include "geometry/terrain_triangulation.hpp"
#include "io/terrain_source.hpp"
#include <QJsonObject>
#include <QString>
#include <map>
#include <optional>
#include <vector>

namespace sketchy {

enum class TerrainImportRole : std::uint8_t {
    Auto,      // polylines: constant Z is a contour, varying Z a breakline; points: ground
    Contour,   // a varying-Z "contour" becomes a breakline with a notice
    Breakline, // point codes: the points in file order form one breakline per code
    Boundary,  // closed; outer limit
    Hole,      // closed; excluded
    Ground,    // spot points (polyline vertices become points too)
    Ignore,
};
std::optional<TerrainImportRole> terrainImportRole(const QString &name);
const char *terrainImportRoleName(TerrainImportRole role);

struct TerrainOriginMetres {
    double easting{}, northing{}, elevation{};
    bool operator==(const TerrainOriginMetres &) const = default;
};

struct TerrainImportOptions {
    std::map<QString, TerrainImportRole> roles; // by DXF layer or point description code
    std::optional<TerrainOriginMetres> origin;  // explicit origin, metres
    TerrainTriangulationOptions triangulation;
    // R095.c2: triangles of the document's other terrains. When non-zero, a mesh that would
    // take the document past TerrainLimits::triangles is refused in the report
    // (`triangle-budget`) instead of by the edit. Import leaves it zero.
    std::size_t otherTerrainTriangles{};
};

struct TerrainPrepared {
    bool ok{};
    TerrainOriginMetres proposed, origin;
    QString originSource;                           // "proposed", "explicit" or "document"
    std::vector<TerrainSource> sources;             // local frame, canonical order, with references
    std::array<double, 3> sourceMin{}, sourceMax{}; // source units
    std::array<double, 3> localMin{}, localMax{};   // metres
    std::map<QString, std::uint64_t> counts;        // complete counts of the preparation
    std::map<QString, std::uint64_t> roles;         // sources by role
    TerrainReport report;                           // the parse problems plus the preparation's
    // Point references by exact local XY, to name a point in adapter locations.
    std::map<std::pair<double, double>, std::vector<TerrainSourceRef>> pointRefs;
};
// Refuses (INVALID_REQUEST) an explicit origin that differs from the document's site origin.
TerrainPrepared prepareTerrainSources(const TerrainSourceSet &set,
                                      const TerrainImportOptions &options,
                                      const std::optional<SiteOrigin> &site);

// Stored sources to adapter input. Source i (0-based) has sourceId i + 1. A contour or
// breakline whose last point repeats its first is closed.
TerrainTriangulationInput terrainTriangulationInput(const std::vector<TerrainSource> &sources);

struct TerrainImportResult {
    bool ok{};
    std::string code; // when refused: INVALID_REQUEST or LIMIT_EXCEEDED
    std::string message;
    TerrainMesh mesh;
    TerrainRecord record;                // meshDigest is set by importTerrain
    std::optional<SiteOrigin> establish; // set when the document has no site origin yet
    TerrainOriginMetres origin, proposed;
    QString originSource;
    std::size_t tiles{};
    // The source report with a bounded "terrain" object; at most 64 KiB in all.
    QJsonObject report;
};
struct TerrainImportProvenance {
    std::string sourceName; // file name for the site provenance
    std::string crs;        // informational label for a new site origin
};
TerrainImportResult runTerrainImport(const TerrainSourceSet &set,
                                     const TerrainImportOptions &options,
                                     const std::optional<SiteOrigin> &site,
                                     const TerrainImportProvenance &provenance = {});

// terrain.inspect_source: extents, layers or codes with counts and default roles, the
// proposed origin, the conversion and the bounded report including the triangulation dry run.
QJsonObject terrainSourceInspection(const TerrainSourceSet &set, const TerrainImportResult &dryRun);

inline constexpr const char *terrainGenerator = "cdt-2.0.0/terrain-v1";

// ---- R095.c2: regenerate, set_parameters and create_from_edges ----------------------------

// The adapter options a record's parameters stand for. Boundary mode uses the stored boundary
// source; the other modes leave it out of the triangulation (it stays stored). The maximum
// edge length applies in MaxEdgeLength mode, and in Boundary mode when it is set; never in
// ConvexHull mode.
TerrainTriangulationOptions terrainTriangulationOptions(const TerrainParameters &parameters);
// Refuses (INVALID_REQUEST) Boundary mode without a stored boundary source and MaxEdgeLength
// mode without a positive length.
void validateTerrainParameters(const TerrainParameters &parameters,
                               const std::vector<TerrainSource> &sources);

// Rebuilds a mesh from stored sources with `parameters`. The result's record is `record` with
// those parameters and the current generator (meshDigest is set by regenerateTerrain). The
// report has the import's shape: {"apiVersion", "format": "terrain-record", "units": "m",
// "metresPerUnit": 1, "terrain": {...}} with XY in the terrain's local metres; `notices` are
// added to it (the paint notice). Refusals set `code` and `message` and change nothing.
TerrainImportResult runTerrainRegenerate(const TerrainRecord &record,
                                         const TerrainParameters &parameters,
                                         std::size_t otherTerrainTriangles,
                                         TerrainReport notices = {});

// Model edges picked for terrain.create_from_edges. An edge is (body, edge ID). `edges` take
// their role from Z (constant: contour; varying: breakline); `contours` and `breaklines`
// force a role (a varying "contour" becomes a breakline with a notice); `boundary` and each
// list of `holes` must chain into closed loops.
struct TerrainEdgeRef {
    Id body{}, edge{};
    auto operator<=>(const TerrainEdgeRef &) const = default;
};
struct TerrainEdgeSelection {
    std::vector<TerrainEdgeRef> edges, contours, breaklines, boundary;
    std::vector<std::vector<TerrainEdgeRef>> holes;
};
// The selection as a source set in model metres (format "edges", unit m, no SHA-256), with
// layers "edges", "contour", "breakline", "boundary" and "hole" for the roles. Each group (and
// each hole list) is chained separately: endpoints within 1e-7 m (world coordinates, through
// the bodies' transforms) are one node; polylines run between nodes that do not have exactly
// two selected edges, and the remaining edges form closed loops. Edges are taken in ascending
// (body, edge) order, so the chaining is canonical; a polyline's reference names its smallest
// edge ("body B edge E"). Refuses (TerrainError) a missing body or edge (NOT_FOUND), an edge
// of a terrain tile (UNSUPPORTED_TARGET, terrainProtectedMessage), a locked body or ancestor
// (INVALID_OPERATION), an edge given twice (INVALID_REQUEST) and an empty selection.
TerrainSourceSet terrainEdgeSources(const Document &doc, const TerrainEdgeSelection &selection);
std::map<QString, TerrainImportRole> terrainEdgeRoles();

} // namespace sketchy
