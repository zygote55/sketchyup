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

} // namespace sketchy
