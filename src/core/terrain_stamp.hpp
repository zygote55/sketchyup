#pragma once
// R096.d building-pad stamp (ADR 0169 §4.3, owner decisions 3, 4 and 6): the edit kind `stamp`.
//
// An entry holds its parameters in the terrain frame (the footprint polygon, the pad elevation,
// the grading mode with its slopes and maximum distance or its band width, and the source body for
// Restamp) and its resolved geometry, which is recomputed whenever the entry applies (a replay
// against a changed base stores the new geometry back) and cleared when a replay skips it:
// - the pad outline as it is in the mesh: the footprint with the edge rays' start points, at the
//   pad elevation;
// - the rays in order around the outline, each with its daylight point; the daylight points in
//   that order (consecutive repeats once) are the daylight line, a closed ring around the pad;
// - the rays the self-intersection repair dropped, and the further runs of source lines it split.
//
// The solver (planTerrainStamp):
// - Rays run outward in plan: from each outline vertex along its outward normal, a fan at no more
//   than 15° spacing around a convex corner (both edge normals included), one bisector ray at a
//   concave corner, and from points along each edge at a spacing no larger than the smallest
//   longest side of the triangles each quarter-cell stretch of the edge touches (at least
//   0.01 m; every stretch starts with a ray).
// - Daylight mode: along a ray the grade is z = h - fill * t while the pad is above the ground at
//   the start and z = h + cut * t while it is below; the ray is walked through the edit mesh and
//   the first crossing with the ground is solved exactly on each triangle's plane. A ray shorter
//   than 0.1 mm meets the ground at the pad edge (the daylight point is the start point, at h).
//   Fixed-width mode: the daylight point is at the width along the ray, at the ground's height.
// - Refusals (located): a ray that leaves the terrain, or reaches the maximum distance without
//   daylighting; a daylight polygon that comes within 1e-6 m of another pad's outline or of the
//   terrain's outline or holes; and the limits below.
// - Self-intersection repair: rays, the daylight line and the outline must not cross. For each
//   crossing, the rays of the shorter run around the outline that spans the crossing elements are
//   dropped, and the check repeats; the outer boundary is kept and the drops are reported.
// - Mesh result: one cavity, the triangles that meet the daylight polygon (closed, widened by
//   1e-6 m) plus the Bowyer–Watson cavity of each inserted vertex, re-triangulated by
//   retriangulateTerrainCavity with the pad outline, every ray and the daylight line as
//   constraints. Every vertex in the widened polygon is removed, so the pad is a flat CDT at h.
// - Current sources: every contour, breakline and spot-point vertex in the widened polygon is
//   clipped and every line segment that meets it is cut (TerrainSourceClipRule); the outline and
//   the daylight line join as closed breaklines, with the first two ids of the entry's block
//   (terrainSplitSourceId ordinals 0 and 1); split runs take ordinals from 2.
// - Pads are protected (decision 6): `protects` gives the footprint and `protectedPoints` the
//   outline's vertices.
#include "core/terrain_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sources.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace sketchy {
inline constexpr const char *terrainStampKind = "stamp";
// Bounds (ADR 0169 §4.3, §7). The ray count, the slope range and the distance range are R096.d's
// choices where the ADR is open.
inline constexpr std::size_t terrainStampFootprintPoints = 20000;
inline constexpr std::size_t terrainStampRays = 32768;
inline constexpr std::size_t terrainStampOutlinePoints = terrainStampFootprintPoints + terrainStampRays;
inline constexpr double terrainStampSlopeMin = 0.01, terrainStampSlopeMax = 100;
inline constexpr double terrainStampDistanceMax = 1000;
inline constexpr double terrainStampWidthMin = 0.01;
inline constexpr double terrainStampSpacingMin = 0.01;
// The daylight polygon is widened by this much (metres) for what it removes and protects against.
inline constexpr double terrainStampSlack = 1e-6;
// A ray shorter than this meets the ground at the pad edge.
inline constexpr double terrainStampTouch = 1e-4;
// Work bounds: triangle tests while walking the rays, edge tests while classifying points
// against the daylight polygon, and segment pairs tested by the repair.
inline constexpr std::uint64_t terrainStampWalkWork = std::uint64_t(1) << 26;
inline constexpr std::uint64_t terrainStampClassifyWork = std::uint64_t(1) << 27;
inline constexpr std::uint64_t terrainStampPairWork = std::uint64_t(1) << 25;

enum class TerrainStampMode { Daylight, Width };
const char *terrainStampModeName(TerrainStampMode mode);

struct TerrainStampRay {
    std::uint32_t start{}; // index of its start point in the outline
    Vec3 daylight;         // terrain frame; the start point at h where the pad meets the ground
    bool operator==(const TerrainStampRay &) const = default;
};
struct TerrainStampData final : TerrainEditData {
    // Parameters (terrain frame).
    std::vector<std::array<double, 2>> footprint; // counter-clockwise, closing edge implied
    double elevation{};
    TerrainStampMode mode{TerrainStampMode::Daylight};
    double cutSlope{}, fillSlope{}, maxDistance{}; // daylight
    double width{};                                // fixed width
    Id sourceBody{}, sourceFace{};                 // Restamp only (0: none)
    // Resolved geometry (empty when the entry did not apply).
    std::vector<std::array<double, 2>> outline; // at `elevation`
    std::vector<TerrainStampRay> rays;
    std::uint32_t dropped{};                    // rays the repair dropped
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pieces; // (parent source, piece id)
    bool resolved() const { return !outline.empty(); }
    bool equals(const TerrainEditData &other) const override;
    std::size_t bytes() const override;
};
// The fields an entry must hold (std::invalid_argument otherwise): a footprint of 3 to 20,000
// finite points within the 1e6 m local bound, no point repeating the one before it, a positive
// area (counter-clockwise; binary64 sum in point order) and at most terrainSculptBandEdges edges
// in a horizontal band; a finite elevation within the bound; for daylight a cut and fill slope of
// 0.01 to 100 and a maximum distance above 0 and at most 1,000 m with no width; for fixed width a
// width of 0.01 to 1,000 m with no slopes or distance; a source face only with a source body; and
// resolved geometry that is empty or consistent (an outline of 3 to terrainStampOutlinePoints
// points, at most terrainStampRays rays in order with starts in the outline and finite daylight
// points, pieces with distinct ids from the split range).
void validateTerrainStampData(const TerrainStampData &data);
// The daylight line of resolved data: the rays' daylight points in order, a point repeating the
// one before it (or the first) once.
std::vector<Vec3> terrainStampRing(const TerrainStampData &data);
// The plan box of the entry's influence (TerrainEditKind::bounds): the outline and daylight line
// when resolved, else the footprint widened by the maximum distance or the width.
std::array<double, 4> terrainStampBounds(const TerrainStampData &data);
// The source ids of the entry's outline and daylight line (ordinals 0 and 1 of its block).
std::optional<std::uint32_t> terrainStampOutlineSource(std::uint32_t edit);
std::optional<std::uint32_t> terrainStampDaylightSource(std::uint32_t edit);

struct TerrainStampPlan {
    TerrainRegionChange change;
    std::shared_ptr<TerrainStampData> resolved; // the entry's data with its resolved geometry
    TerrainSculptClip clip;
    std::size_t rays{}, touching{}, cut{}, fill{}, dropped{};
    std::size_t cavityTriangles{}, addedTriangles{}, removedVertices{};
    std::uint64_t flatRemaining{};
    TerrainVec2 droppedAt{}; // the first dropped ray's daylight point (terrain frame)
};
// The stamp of `edit` against the mesh and current sources before it. Throws TerrainEditRefused
// with located problems ("outside-outline", "inside-pad", "daylight-distance", "invalid-polygon",
// "daylight-repair", "limit-exceeded", the adapter's kinds) and TerrainCancelled.
TerrainStampPlan planTerrainStamp(const TerrainEditContext &context, const TerrainEdit &edit);

// The kind (registered as built-in by core/terrain_edit.cpp).
std::vector<TerrainEditKind> terrainStampKinds();

// ---- Check C3 (ADR 0169 §6) ------------------------------------------------------------------
// For every applied stamp of the record (entries named in `skipped` are left out): every mesh
// vertex on or inside its footprint (or at an outline vertex) has Z bit-equal to the pad
// elevation; its outline edges are mesh edges; and along every unsuperseded ray with length, the
// ray is a mesh edge, the daylight point is a mesh vertex with its stored Z, and the grade
// |h - z| / run equals the fill slope (z below h) or the cut slope (z above h) within 1e-9
// relative (daylight), or the run equals the width within 1e-9 relative (fixed width). A ray is
// superseded by a later applied entry whose influence reaches it: a stamp whose widened daylight
// polygon meets it, or any other kind whose plan box meets the ray's.
struct TerrainPadConsistency {
    std::size_t pads{}, padVertices{}, outlineEdges{}, rays{}, superseded{};
    std::size_t wrongElevations{}, missingEdges{}, wrongRays{};
    std::vector<std::string> problems; // at most 32
    bool ok() const { return !wrongElevations && !missingEdges && !wrongRays; }
};
TerrainPadConsistency checkTerrainPadConsistency(const Document &doc, Id terrain,
                                                 const std::vector<std::uint32_t> &skipped = {});
} // namespace sketchy
