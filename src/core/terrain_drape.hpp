#pragma once
// R096.e projection and drape (ADR 0169 §4.4, owner decisions 5, 6 and 7): the edit kind `drape`
// (imprint) and the plan that overlay shares with it.
//
// Lines are plan polylines in the terrain frame (edge chains and face loops, projected vertically;
// their own Z is not used). The plan (planTerrainDrape):
// - The lines are first made to meet only at shared points: input points within 1e-7 m of each
//   other are one point (the lexicographically smallest), a line's point within 1e-7 m of another
//   line's segment is inserted into it, and two segments that cross take their crossing point.
// - Each segment is then split against the mesh: at every mesh vertex within 1e-7 m of it (the
//   segment is bent through the vertex) and at every crossing with a mesh edge. A point within
//   1e-9 m of an edge (and not near a vertex) lies on that edge. Elevations come from the surface:
//   a vertex keeps its own, a point on an edge is interpolated along the edge from its
//   lexicographically smaller end (so both triangles agree, and a contour edge gives its exact
//   elevation), a point inside a triangle is interpolated in that triangle's plane. Parts outside
//   the outline or in a hole are clipped (counted, with a location); the line continues as a new
//   run where it re-enters.
// - Imprint splits only the triangles that hold a new vertex, each re-triangulated within itself
//   (retriangulateTerrainCavity on the triangle with its edge points and inner points, the pieces
//   inside it as constraints, no flat-triangle flip): every new triangle lies in the plane of the
//   triangle it came from, so the surface's shape does not change (check C4). A new triangle below
//   the modelling tolerance (twice its area under 1e-12 m²) is refused with its location.
// - Resolved geometry, recomputed whenever the entry applies: the runs as they are in the mesh
//   (they join the current sources as breaklines, with ids from the entry's block,
//   terrainSplitSourceId ordinals 0 up), and the existing constraint segments a new vertex split
//   (contours, breaklines and pad rays), with the points inserted into them.
// - Pads (decision 6): a run that meets a pad's footprint or outline is refused ("inside-pad"); a
//   run may cross a pad's slopes, whose crossed rays it supersedes (as a later stamp would).
// - Imprinted lines are the entry's own: terrain.edit_source and terrain.remove_source refuse them
//   ("drape-source"), as they refuse a pad's outline (remove the drape with terrain.remove_edit).
//
// Paint regions (decision 7): an imprint entry may record face loops with a material. Every tile
// triangle whose centroid lies inside a region (even-odd over its loops; the last region wins)
// takes the material on both sides, whenever its tile is built (an edit, a replay, Regenerate);
// an imprint that adds a region repaints the faces of untouched tiles inside it.
#include "core/terrain_edit.hpp"
#include "core/terrain_sources.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace sketchy {
inline constexpr const char *terrainDrapeKind = "drape";
// Bounds (ADR 0169 §4.4, §7; R096.e's choices where the ADR is open).
inline constexpr std::size_t terrainDrapeLines = 4096;          // lines per entry
inline constexpr std::size_t terrainDrapePoints = 20000;        // input points per entry, in all
inline constexpr std::size_t terrainDrapePaintRegions = 1024;   // paint regions per entry
inline constexpr std::size_t terrainDrapeRuns = 4096;           // resolved runs (source ids)
inline constexpr std::size_t terrainDrapeResolvedPoints = 65536; // resolved run points, in all
inline constexpr std::size_t terrainDrapeSplitPoints = 65536;   // points inserted into sources
inline constexpr std::size_t terrainDrapeTriangles = 65536;     // triangles split
inline constexpr std::size_t terrainDrapeTouched = 16 * 65536;  // triangles found by segments
inline constexpr std::size_t terrainDrapeCrossings = 65536;     // crossings among the lines
inline constexpr std::uint64_t terrainDrapePairWork = std::uint64_t(1) << 25; // segment pairs
inline constexpr std::uint64_t terrainDrapeScanWork = std::uint64_t(1) << 27; // triangles scanned
// The sum over a terrain's paint regions of their loops' busiest horizontal band (edges one
// containment test reads): a centroid test reads at most this many edges.
inline constexpr std::size_t terrainDrapePaintBandEdges = 4096;
inline constexpr double terrainDrapeSnap = 1e-7;     // to a mesh vertex or another line's point
inline constexpr double terrainDrapeEdgeSnap = 1e-9; // onto a mesh edge
inline constexpr double terrainDrapeLiftDefault = 0.01, terrainDrapeLiftMax = 1000;
// Overlay: faces it may create, and triangles it may classify.
inline constexpr std::uint64_t terrainDrapeOverlayWork = std::uint64_t(1) << 22;

struct TerrainDrapeLine {
    std::vector<std::array<double, 2>> points; // terrain frame; a closed line does not repeat
    bool closed{};
    bool operator==(const TerrainDrapeLine &) const = default;
};
struct TerrainDrapePaint {
    std::vector<std::vector<std::array<double, 2>>> loops; // outer and inner loops, closing implied
    Id material{};
    bool operator==(const TerrainDrapePaint &) const = default;
};
// An existing constraint segment (a, b: plan, a lexicographically smaller) split by new vertices,
// listed in order from a.
struct TerrainDrapeSplit {
    std::array<double, 2> a{}, b{};
    std::vector<Vec3> points;
    bool operator==(const TerrainDrapeSplit &) const = default;
};
struct TerrainDrapeData final : TerrainEditData {
    // Parameters (terrain frame).
    std::vector<TerrainDrapeLine> lines;
    std::vector<TerrainDrapePaint> paint;
    // Resolved geometry (empty when the entry did not apply).
    std::vector<std::vector<Vec3>> runs; // a closed run repeats its first point
    std::vector<TerrainDrapeSplit> splits;
    bool resolved() const { return !runs.empty(); }
    bool equals(const TerrainEditData &other) const override;
    std::size_t bytes() const override;
    std::vector<Id> materials() const override;
};
// The fields an entry must hold (std::invalid_argument otherwise): 1 to 4,096 lines of 2 (3 when
// closed) or more finite points within the 1e6 m local bound, no point repeating the one before
// it, 20,000 points in all; at most 1,024 paint regions, each of 1 or more loops of 3 or more
// such points with at most terrainSculptBandEdges edges in a horizontal band and a material; and
// resolved geometry that is empty or consistent (1 to 4,096 runs of 2 or more finite points,
// 65,536 points in all; splits of distinct segments with 1 or more points, 65,536 in all).
void validateTerrainDrapeData(const TerrainDrapeData &data);
// The plan box of the entry's influence (TerrainEditKind::bounds): its lines and paint loops.
std::array<double, 4> terrainDrapeBounds(const TerrainDrapeData &data);

struct TerrainDrapePiece {
    TerrainTriangleRef triangle;     // the split triangle
    std::uint32_t first{}, count{};  // its new triangles in change.added
};
struct TerrainDrapePlan {
    TerrainRegionChange change;
    std::shared_ptr<TerrainDrapeData> resolved; // the entry's data with its resolved geometry
    std::vector<TerrainDrapePiece> pieces;      // in TerrainTriangleRef order
    std::vector<std::uint32_t> runLines;        // the input line of each run
    std::size_t clipped{};      // parts of lines outside the outline or in a hole
    TerrainVec2 clippedAt{};    // the first clipped part's middle (terrain frame)
    std::size_t intersections{}; // points the lines gained where they meet each other
    std::size_t crossings{}, snapped{}, inserted{}; // mesh-edge crossings, vertices, new vertices
    std::size_t splitSegments{};
    std::size_t points{};        // resolved run points
};
// The drape of `edit` against the mesh and current sources before it. Overlay (`overlay`) skips
// the pad rule and the source-id block (it changes no record). Throws TerrainEditRefused with
// located problems ("outside-outline" when nothing is left inside, "inside-pad",
// "degenerate-triangle", "limit-exceeded", the adapter's kinds) and TerrainCancelled.
TerrainDrapePlan planTerrainDrape(const TerrainEditContext &context, const TerrainEdit &edit,
                                  bool overlay = false);

// Overlay geometry (ADR 0169 §4.4): the runs of `edgeLines` as edges, and for each face (its
// loops, in plan) one planar polygon per terrain-triangle piece inside it (a later face wins
// where faces overlap), each lifted by `lift`. Terrain frame. Throws TerrainEditRefused
// ("limit-exceeded") past terrainDrapeOverlayWork classified triangles or faceLimit faces.
struct TerrainDrapeOverlayFace {
    std::size_t face{};                     // index into `faces`
    std::vector<std::vector<Vec3>> loops;   // counter-clockwise outer loop, then clockwise holes
};
struct TerrainDrapeOverlay {
    std::vector<TerrainDrapeOverlayFace> faces;
    std::vector<std::array<Vec3, 2>> edges;
};
TerrainDrapeOverlay terrainDrapeOverlay(const TerrainEditMesh &mesh, const TerrainDrapePlan &plan,
                                        const std::vector<bool> &edgeLines,
                                        const std::vector<std::vector<std::vector<std::array<double, 2>>>> &faces,
                                        double lift, std::size_t faceLimit,
                                        TerrainProgress *progress = nullptr);

// The kind (registered as built-in by core/terrain_edit.cpp).
std::vector<TerrainEditKind> terrainDrapeKinds();

// ---- Paint regions (decision 7) ---------------------------------------------------------------
// A paint loop indexed by horizontal bands (R096.b's band layout): an even-odd test reads one band.
class TerrainPaintLoop;
class TerrainPaintRegions {
  public:
    TerrainPaintRegions() = default;
    // The regions of the record's applied drape entries, in list order.
    explicit TerrainPaintRegions(const TerrainRecord &record);
    bool empty() const { return regions_.empty(); }
    std::size_t size() const { return regions_.size(); }
    // The material of the last region holding (x, y), or 0. Regions from `first` on only.
    Id materialAt(double x, double y, std::size_t first = 0) const;
    // Whether any region from `first` on has a box meeting the box.
    bool meets(const std::array<double, 4> &box, std::size_t first = 0) const;

  private:
    struct Region {
        Id material{};
        std::vector<std::shared_ptr<const TerrainPaintLoop>> loops;
        std::array<double, 4> box{};
    };
    std::vector<Region> regions_;
};
// Paints a tile body's faces by their centroids from the regions (from `first` on): a face inside
// a region takes its material on both sides (no per-face entry when that equals the tile's own
// materials); other faces are left as they are. Returns whether the body changed.
bool paintTerrainTile(Body &tile, const TerrainPaintRegions &regions, std::size_t first = 0);
// Whether a face's per-face paint is exactly what the regions give it (so a rebuild that paints it
// again drops nothing): no colour or texture, and its material sides those of the region holding
// its centroid.
bool terrainFacePaintDerived(const Body &tile, Id face, const TerrainPaintRegions &regions);
// The sum over the record's paint loops of their busiest band (refused past
// terrainDrapePaintBandEdges by validateTerrainRecord).
std::size_t terrainPaintBandEdges(const TerrainRecord &record);
} // namespace sketchy
