#pragma once
// R095.c terrain sources (ADR 0168 section 1): the DXF terrain mode and point files
// (CSV / XYZ / PNEZD) parsed into one source set in *source units*, and the bounded
// report shared by the parsers, the preparation step and the commands.
//
// Every item keeps the reference that names it in the file (a DXF entity handle, the
// 1-based entity number when the entity has no handle, or a 1-based line number) and its
// layer or description code. Structural failures (budgets, units, malformed DXF) throw one
// message, as the other importers do; located value problems in point files are collected
// into the report so every bad line can be named.
#include "geometry/terrain_triangulation.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sketchy {

// Units a terrain source can be in. Elevation always uses the horizontal unit.
inline constexpr double usSurveyFootMetres = 1200.0 / 3937.0;
struct TerrainUnit {
    QString name;           // "mm", "cm", "m", "in", "ft", "usft"
    double metresPerUnit{}; // the double used for the conversion
    QString exact;          // the exact ratio, e.g. "0.3048" or "1200/3937"
};
const std::vector<TerrainUnit> &terrainUnits();
std::optional<TerrainUnit> terrainUnit(const QString &name);
QString terrainUnitNames(); // "mm, cm, m, in, ft or usft"

// Where an item comes from.
struct TerrainSourceRef {
    enum class Kind : std::uint8_t { Handle, Entity, Line };
    Kind kind{Kind::Line};
    std::uint64_t value{};
    std::string text() const; // "handle 2F", "entity 12" or "line 7"
    auto operator<=>(const TerrainSourceRef &) const = default;
};
struct TerrainSourcePolyline {
    TerrainSourceRef ref;
    std::uint32_t layer{};           // index into TerrainSourceSet::layers
    std::vector<TerrainVec3> points; // source units; a closed polyline does not repeat its start
    bool closed{};
};
struct TerrainSourcePoint {
    TerrainSourceRef ref;
    std::uint32_t layer{}; // description code
    TerrainVec3 position;
};
struct TerrainSourceLayer {
    QString name; // DXF layer, or point description code ("" for none)
    std::uint64_t polylines{}, closed{}, constantZ{}, varyingZ{}, points{};
};

// The bounded report (ADR 0168 section 1, "Invalid input and bounded reports"): problems
// grouped by kind, each with a complete count and at most 32 located entries; at most 64
// kinds; messages at most 256 bytes quoting at most 64 bytes of user text; the JSON at most
// 64 KiB (locations are thinned evenly until it fits, counts never are).
struct TerrainReportBounds {
    static constexpr std::size_t locationsPerKind = 32;
    static constexpr std::size_t kinds = 64;
    static constexpr std::size_t messageBytes = 256;
    static constexpr std::size_t quotedBytes = 64;
    static constexpr std::size_t reportBytes = 64 * 1024;
};
// User text quoted in a message: control characters replaced, cut on a UTF-8 boundary at
// 64 bytes with an ellipsis.
std::string terrainQuote(const std::string &text);
struct TerrainReportLocation {
    TerrainReportLocation() = default;
    TerrainReportLocation(std::string reference, std::string reference2 = {},
                          std::optional<std::array<double, 2>> xy = std::nullopt,
                          std::string detail = {})
        : reference(std::move(reference)), reference2(std::move(reference2)), xy(xy),
          detail(std::move(detail)) {}
    std::string reference;                   // source reference of the item
    std::string reference2;                  // the second item, for pairs
    std::optional<std::array<double, 2>> xy; // source units
    std::string detail; // optional; at most 256 bytes, user text quoted with terrainQuote
};
class TerrainReport {
  public:
    enum class Severity { Refusal, Notice };
    // Adds `count` occurrences of `kind`; `message` describes the kind (the first one given
    // is kept). Counts saturate at UINT64_MAX and are always complete.
    void add(Severity severity, const std::string &kind, std::uint64_t count,
             const std::string &message);
    // One located occurrence; counts one more occurrence when `counted` is true.
    void locate(Severity severity, const std::string &kind, const std::string &message,
                TerrainReportLocation location, bool counted = true);
    bool refused() const;
    bool hasNotices() const;
    std::uint64_t count(const std::string &kind) const;
    // `envelope` (the other fields of the terrain object) plus "refusals", "notices",
    // "kindsOmitted", "locationsPerKind" and "locationsThinned", in at most `budget` bytes of
    // compact JSON when that is reachable by thinning locations.
    QJsonObject json(QJsonObject envelope = {},
                     std::size_t budget = TerrainReportBounds::reportBytes) const;

  private:
    struct Kind {
        Severity severity{};
        std::string name, message;
        std::uint64_t count{};
        std::vector<TerrainReportLocation> locations;
    };
    std::vector<Kind> kinds_;
    std::vector<std::string> omittedNames_; // kinds past the 64-kind bound
    std::uint64_t omittedOccurrences_{};
    bool omittedRefusal_{};
    Kind *find(Severity severity, const std::string &kind, const std::string &message);
};

struct TerrainSourceSet {
    QString format; // "dxf" or "points"
    TerrainUnit unit;
    QString sha256;
    std::vector<TerrainSourceLayer> layers;
    std::vector<TerrainSourcePolyline> polylines;
    std::vector<TerrainSourcePoint> points;
    // Problems found while parsing (located point-file values, arc segmentation,
    // ignored entities).
    TerrainReport problems;
    // The source report in the existing importer shape: apiVersion, format, metresPerUnit,
    // sha256, bytes and the format's counts.
    QJsonObject report;
};

// Budgets (ADR 0168 section 1).
struct TerrainSourceLimits {
    static constexpr qsizetype bytes = 64 * 1024 * 1024;
    static constexpr std::size_t pointLines = 1000000;
    static constexpr std::size_t lineBytes = 4096;
    static constexpr std::size_t vertices = 1000000; // raw source vertices, both formats
    static constexpr std::size_t layers = 1024;      // DXF layers or description codes
    static constexpr double coordinate = 1e8;        // |value| in metres, the site bound
};

// DXF terrain mode. Accepts LWPOLYLINE (group 38 elevation), 2D POLYLINE (vertex Z, or the
// polyline elevation where a vertex has none), 3D POLYLINE (flag 8) and LINE with equal end
// Z (each its own two-point polyline). Bulges become chords within the chord tolerance
// (model metres). Other entities are counted and ignored. Units follow $INSUNITS or the
// explicit unit; unitless input is refused. The planar reader (parseDxf) is unchanged.
struct DxfTerrainOptions {
    std::optional<TerrainUnit> unit; // explicit; otherwise $INSUNITS
    double chordTolerance{0.05};     // metres
};
TerrainSourceSet parseDxfTerrain(const QByteArray &bytes, const DxfTerrainOptions &options = {});

// Point files.
enum class TerrainColumn : std::uint8_t {
    Point,
    Easting,
    Northing,
    Elevation,
    Description,
    Ignore
};
enum class TerrainDelimiter : std::uint8_t { Comma, Tab, Semicolon, Whitespace };
struct TerrainPointOptions {
    TerrainUnit unit;                          // required
    std::optional<TerrainDelimiter> delimiter; // detected when absent
    std::optional<bool> header;                // detected when absent
    std::optional<std::vector<TerrainColumn>> columns;
    QString preset; // the preset name the columns came from, for the report
};
// "PNEZD", "PENZD", "ENZ", "XYZ" (X = easting, Y = northing).
std::optional<std::vector<TerrainColumn>> terrainColumnPreset(const QString &name);
std::optional<TerrainColumn> terrainColumn(const QString &name);
std::optional<TerrainDelimiter> terrainDelimiter(const QString &name);
const char *terrainColumnName(TerrainColumn column);
const char *terrainDelimiterName(TerrainDelimiter delimiter);
TerrainSourceSet parseTerrainPoints(const QByteArray &bytes, const TerrainPointOptions &options);

} // namespace sketchy
