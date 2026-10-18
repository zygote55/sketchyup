#pragma once
// R095.b (ADR 0168 section 3): terrain triangulation behind the vendored CDT library.
//
// Plain data in, plain data out: local metres as binary64 in, local XYZ vertices and
// triangle indices out, plus a bounded report. Standard library only; the CDT headers
// are included by the .cpp alone. The caller converts source coordinates to the terrain's
// local frame first: translating the same data into UTM changes co-circular ties and loses
// precision (ADR section 3, Determinism), so any coordinate beyond the local bound
// (limits.maxCoordinate, 1e6 m) is refused.
//
// Determinism: the same input in the same order gives the same output bit for bit on one
// build. Input order matters. Vertices are numbered by first occurrence (contours, then
// breaklines, then points, then boundary and hole vertices that are not already present),
// and co-circular ties resolve by insertion. Canonical ordering of the sources is the
// caller's job (ADR section 3).
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sketchy {

struct TerrainVec2 {
    double x{0};
    double y{0};
    bool operator==(const TerrainVec2 &) const = default;
};
struct TerrainVec3 {
    double x{0};
    double y{0};
    double z{0};
    bool operator==(const TerrainVec3 &) const = default;
};

// A polyline at one elevation (a contour). `sourceId` is the caller's reference (a DXF
// entity handle, a line number) echoed in report locations; it is never interpreted.
struct TerrainContour {
    double z{0};
    std::vector<TerrainVec2> points;
    bool closed{false};
    std::uint64_t sourceId{0};
};
// A polyline whose vertices carry their own elevations.
struct TerrainBreakline {
    std::vector<TerrainVec3> points;
    bool closed{false};
    std::uint64_t sourceId{0};
};
struct TerrainPoint {
    TerrainVec3 position;
    std::uint64_t sourceId{0};
};
// A closed polygon (the closing edge is implied). Elevations are not supplied: a polygon
// vertex that coincides with a source vertex takes its elevation, any other is interpolated
// from the triangulation of the sources (nearest source vertex outside their hull).
struct TerrainPolygon {
    std::vector<TerrainVec2> points;
    std::uint64_t sourceId{0};
};

struct TerrainTriangulationInput {
    std::vector<TerrainContour> contours;
    std::vector<TerrainBreakline> breaklines;
    std::vector<TerrainPoint> points;
    std::optional<TerrainPolygon> boundary;
    std::vector<TerrainPolygon> holes;
};

enum class TerrainFlatMode : std::uint8_t {
    Flip,       // flip flat triangles where a flip keeps the constraints, report the rest
    ReportOnly, // leave the Delaunay result alone and report flat triangles
};

struct TerrainTriangulationLimits {
    std::size_t maxTriangles{250000};    // ADR 0168 section 2
    std::size_t maxVertices{250000};     // after merging, ADR 0168 section 1
    std::size_t maxRawVertices{1000000}; // before merging; bounds work on hostile input
    std::size_t maxPolygonVertices{20000};
    double maxCoordinate{1e6}; // |x|, |y| and |z| in the local frame, ADR 0168 section 1
};

struct TerrainTriangulationOptions {
    TerrainFlatMode flatMode{TerrainFlatMode::Flip};
    // Off by default: it invents elevations a survey did not contain (ADR 0168 owner decision 6).
    // Each remaining flat triangle is split at the midpoint of its longest eligible edge
    // with Z halfway to the opposite vertex of the neighbouring triangle.
    bool steinerFill{false};
    // Without a boundary, repeatedly remove triangles whose outer, non-constraint edge is
    // longer than this (metres, in plan). Empty means no limit.
    std::optional<double> maxEdgeLength;
    // Vertices closer than this in x and y are one vertex; with different Z they conflict.
    double xyTolerance{1e-7};
    double zTolerance{1e-9};
    // Elevations within this are the same contour elevation.
    double flatTolerance{1e-9};
    TerrainTriangulationLimits limits;
};

enum class TerrainProblemKind : std::uint8_t {
    NonFinite,            // a coordinate is NaN or infinite
    ExtentExceeded,       // beyond the local frame's coordinate bound
    LimitExceeded,        // too many input vertices or polygon vertices
    ConflictingDuplicate, // same XY, different Z
    CrossingConstraints,  // crossing, touching or overlapping constraint segments
    InsufficientPoints,   // fewer than 3 non-collinear points
    InvalidPolygon,       // boundary or hole with fewer than 3 distinct vertices, or hole inside a hole
    TriangleBudget,       // more triangles than the limit
    TriangulationFailed,  // the library reported an error, or an internal check failed
};
inline constexpr std::size_t terrainProblemKindCount = 9;
const char *terrainProblemKindName(TerrainProblemKind kind);

inline constexpr std::size_t terrainMaxLocationsPerKind = 32; // ADR 0168 section 1
inline constexpr std::size_t terrainMaxMessageBytes = 256;

struct TerrainLocation {
    TerrainVec2 xy;
    std::uint64_t sourceA{0}; // the sourceId involved, or of the first segment
    std::uint64_t sourceB{0}; // the second segment's, for pairs
};

struct TerrainProblem {
    TerrainProblemKind kind{TerrainProblemKind::NonFinite};
    std::uint64_t count{0};                 // always complete, up to the counting caps in the .cpp
    std::vector<TerrainLocation> locations; // at most terrainMaxLocationsPerKind
    std::string message;                    // at most terrainMaxMessageBytes
};

struct TerrainTriangulationReport {
    // Input.
    std::uint64_t rawVertexCount{0};
    std::uint64_t uniqueVertexCount{0};   // after merging duplicates
    std::uint64_t mergedDuplicates{0};    // vertices merged into an earlier one
    std::uint64_t mergedSegments{0};      // repeated segments merged into one constraint
    std::uint64_t droppedZeroLengthSegments{0};
    std::uint64_t droppedOutsideVertices{0}; // outside the boundary or inside a hole
    std::uint64_t droppedOutsideSegments{0}; // constraint segments lost with them
    std::uint64_t droppedHoles{0};           // holes entirely outside the boundary
    std::uint64_t interpolatedPolygonVertices{0};
    std::uint64_t extrapolatedPolygonVertices{0}; // outside the sources' hull: nearest source Z
    std::uint64_t constraintEdges{0};
    // Output.
    std::uint64_t vertexCount{0};
    std::uint64_t triangleCount{0};
    std::uint64_t trimmedTriangles{0};  // removed by maxEdgeLength
    std::uint64_t unusedVertices{0};    // present in the input, in no output triangle
    // Flat triangles.
    std::uint64_t flatTrianglesInitial{0};
    std::uint64_t flipsApplied{0};
    std::uint64_t steinerPointsAdded{0};
    std::uint64_t flatTrianglesRemaining{0};
    double flatAreaRemaining{0}; // square metres in plan
    std::vector<TerrainVec2> flatLocations; // centroids, at most terrainMaxLocationsPerKind
    // Refusals, in TerrainProblemKind order.
    std::vector<TerrainProblem> refusals;
};

struct TerrainTriangulationResult {
    bool ok{false};
    std::vector<TerrainVec3> vertices;
    // Counter-clockwise in plan. Each triangle starts at its smallest vertex index and the
    // list is sorted, so the order does not depend on the library's internals.
    std::vector<std::array<std::uint32_t, 3>> triangles;
    TerrainTriangulationReport report;
};

// Never throws for bad input; a refusal leaves `ok` false and the vertices and triangles
// empty. Throws std::bad_alloc only when memory is exhausted.
TerrainTriangulationResult triangulateTerrain(const TerrainTriangulationInput &input,
                                              const TerrainTriangulationOptions &options = {});

} // namespace sketchy
