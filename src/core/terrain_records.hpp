#pragma once
#include "geometry/surface.hpp"
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
// R095.a terrain and site records (ADR 0168 §2, §4). Records only: the operations
// that build, check and convert them are in core/terrain.hpp.
namespace sketchy {
// A source polyline or point set in the terrain's local frame (the terrain body's
// frame, which its tiles share), with the reference that names it in the source
// file: a DXF entity handle, a CSV line number or a model edge ID.
enum class TerrainSourceRole { Contour, Breakline, Boundary, Hole, Points };
struct TerrainSource {
    TerrainSourceRole role{TerrainSourceRole::Contour};
    std::string reference;
    std::vector<Vec3> points;
    bool operator==(const TerrainSource &) const = default;
};
// Triangulation parameters recorded for Regenerate (ADR 0168 §3).
enum class TerrainBoundaryMode { ConvexHull, MaxEdgeLength, Boundary };
struct TerrainParameters {
    TerrainBoundaryMode boundary{TerrainBoundaryMode::MaxEdgeLength};
    double maxEdgeLength{}; // Metres; zero means "not set" (only valid for other modes).
    bool flipFlatTriangles{true}, steinerFill{false};
    bool operator==(const TerrainParameters &) const = default;
};
// Import provenance: source file digest, its unit name and the unit scale.
struct TerrainProvenance {
    std::string sha256; // Lowercase hex, or empty for terrains built from model edges.
    std::string units{"m"};
    double metresPerUnit{1};
    bool operator==(const TerrainProvenance &) const = default;
};
struct TerrainRecord {
    std::vector<TerrainSource> sources;
    TerrainParameters parameters;
    std::string generator;  // e.g. "cdt-2.0.0/terrain-v1"
    TerrainProvenance provenance;
    std::string meshDigest; // terrainMeshDigest of the tiles when generated.
    bool operator==(const TerrainRecord &) const = default;
};
struct TerrainLimits {
    // Per document, in its own limit class (ADR 0168 decision 3).
    static constexpr std::size_t triangles = 250000;
    static constexpr std::size_t tileTriangles = 2048;
    static constexpr std::size_t sourceVertices = 250000;
    static constexpr std::size_t sources = 100000;
    static constexpr std::size_t referenceBytes = 128;
    static constexpr std::size_t generatorBytes = 64;
    static constexpr std::size_t unitBytes = 16;
};
// D-043: a terrain edit whose record and tile changes exceed Document::terrainHistoryLimit.
inline constexpr const char *terrainHistoryMessage =
    "The terrain edit is too large for its 512 MiB undo history budget";
inline constexpr const char *terrainProtectedMessage =
    "Edit terrain with the terrain tools, or Explode it to ordinary geometry";
// Refusals that carry one of the shared automation error codes
// (UNSUPPORTED_TARGET, LIMIT_EXCEEDED, NOT_FOUND, INVALID_REQUEST).
class TerrainError : public std::runtime_error {
  public:
    TerrainError(std::string code, const std::string &message)
        : std::runtime_error(message), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
void validateTerrainRecord(const TerrainRecord &record);
std::size_t terrainRecordBytes(const TerrainRecord &record);
// Document site origin (ADR 0168 §4): projected metres; the CRS is a label only.
struct SiteProvenance {
    std::string source, sha256;
    bool operator==(const SiteProvenance &) const = default;
};
struct SiteOrigin {
    double easting{}, northing{}, elevation{};
    std::string crs;
    SiteProvenance established;
    bool operator==(const SiteOrigin &) const = default;
};
// Site coordinates are bounded so that model = site - origin stays exact to well
// below the 1e-7 m tolerance; projected CRSs stay far inside this range.
inline constexpr double siteCoordinateLimit = 1e8;
inline constexpr std::size_t siteCrsBytes = 64, siteSourceBytes = 256;
void validateSiteOrigin(const SiteOrigin &origin);
} // namespace sketchy
