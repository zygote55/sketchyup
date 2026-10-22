#pragma once
// R096.b sculpt and smooth (ADR 0169 §4.1): the surface edits `sculpt` (raise or lower) and
// `smooth` as edit kinds. Only Z changes; the plan triangulation is kept, so nothing is
// re-triangulated.
//
// - Scope: a brush (a path of 1 to 256 plan points, a radius of 0.01 to 1,000 m and a falloff
//   smooth, linear or constant) or a region (one closed polygon of at most 20,000 vertices with a
//   falloff width of 0 to 1,000 m outside it). The weight of a plan position is
//   TerrainSculptInfluence::weight: f(d / radius) by the plan distance d to the whole path, or 1
//   inside the region (closed, nonzero winding) and f(d / width) outside it. The influence is a
//   vertical cylinder (Blender's "projected" falloff).
// - Raise: z' = z + amount * w. The stroke is swept, not accumulated: each vertex moves once, by
//   its distance to the whole path, so pointer sampling does not matter and replay is exact.
// - Smooth: Jacobi iterations toward the plan inverse-distance weighted mean of the one-ring
//   neighbours, z' = z + strength * w * (mean - z); each iteration reads only the previous one's
//   values; neighbours outside the influence are read and never written.
// - Pads (decision 6): vertices on or inside a protected polygon (TerrainEditKind::protects,
//   R096.d's pads) have weight zero.
// - Contours (decision 2): every current contour, breakline and spot-point vertex with a weight
//   above zero that no pad protects is clipped out of the current sources (it is edited ground
//   now), whether or not its Z changed, so the current sources are derived without the mesh
//   (ADR 0169 §2) and check C1 holds for the rest. A line keeps its id for its first remaining run
//   (in point order; a ring from its first clipped vertex); each further run is a new source with
//   an id from terrainSplitSourceId; runs of one vertex are dropped. Boundary and hole sources
//   are not clipped: they define the outline, whose plan the edits never change.
#include "core/terrain_edit.hpp"
#include "core/terrain_fwd.hpp"
#include "core/terrain_sources.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace sketchy {
inline constexpr const char *terrainSculptKind = "sculpt";
inline constexpr const char *terrainSmoothKind = "smooth";
// Bounds (ADR 0169 §4.1, §7). Amounts are a choice of R096.b (the ADR leaves them open).
inline constexpr std::size_t terrainSculptPathPoints = 256;
inline constexpr std::size_t terrainSculptRegionPoints = 20000;
inline constexpr double terrainSculptRadiusMin = 0.01;
inline constexpr double terrainSculptRadiusMax = 1000;
inline constexpr double terrainSculptWidthMax = 1000;
inline constexpr double terrainSculptAmountMax = 1000;
inline constexpr int terrainSmoothIterationsMax = 8;
// Source lines one entry may split into further runs, and the entry ids that may split
// (terrainSplitSourceId's range).
inline constexpr std::uint32_t terrainSculptPiecesPerEdit = 4096;
inline constexpr std::uint32_t terrainSculptSplitEdits = 1u << 18;

const char *terrainFalloffName(TerrainFalloff falloff);
std::optional<TerrainFalloff> terrainFalloffOf(const std::string &name);
// f(t) for t = d / radius: 1 at 0, 0 at 1 and beyond. Smooth is 1 - t^2 (3 - 2t); linear 1 - t;
// constant 1 below 1.
double terrainFalloff(TerrainFalloff falloff, double t);

// One entry of either kind, in the terrain frame. A brush has `path` and `radius`; a region has
// `region` (closing edge implied, the first point not repeated) and `width`. `sculpt` has
// `amount`; `smooth` has `strength` and `iterations`.
struct TerrainSculptData final : TerrainEditData {
    std::vector<std::array<double, 2>> path;
    double radius{};
    std::vector<std::array<double, 2>> region;
    double width{};
    TerrainFalloff falloff{TerrainFalloff::Smooth};
    double amount{};
    double strength{};
    int iterations{};
    // The further runs of source lines it split at its commit, as (parent source id, piece id)
    // in the order they were made (resolved geometry, ADR 0169 §2): on replay a parent's runs take
    // its recorded ids first, so piece ids stay stable when other sources change.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pieces;
    bool brush() const { return !path.empty(); }
    bool equals(const TerrainEditData &other) const override;
    std::size_t bytes() const override;
};
// The fields an entry of `kind` must hold (std::invalid_argument otherwise): exactly one scope;
// a brush of 1 to 256 finite points within the 1e6 m local bound and a radius of 0.01 to 1,000;
// a region of 3 to 20,000 such points, not all on one line (exact orientation), no point
// repeating the one before it, at most terrainSculptBandEdges edges in a band and a width of 0
// to 1,000; recorded pieces with ids from the split range; for sculpt an amount with 0 < |amount| <= 1,000 and no smooth fields; for smooth
// a strength in (0, 1], 1 to 8 iterations and no amount.
void validateTerrainSculptData(const std::string &kind, const TerrainSculptData &data);

// The weight of a plan position (terrain frame), a pure function of the entry: the same bits for
// the same position wherever it is evaluated (the mesh edit, the source clipping and the preview).
// Every segment's distance is computed from its lexicographically smaller end, so a path and its
// reverse, or a path with repeated points, weigh alike bit for bit.
class TerrainSculptInfluence {
  public:
    explicit TerrainSculptInfluence(const TerrainSculptData &data);
    // minX, minY, maxX, maxY: every position outside weighs zero.
    const std::array<double, 4> &bounds() const { return box_; }
    double weight(double x, double y) const;
    // The plan distance to the path (brush) or to the region's edges (region; zero inside is
    // not implied), and whether a region contains the position (closed).
    double distance(double x, double y) const;
    bool inside(double x, double y) const;

  private:
    struct Segment {
        std::array<double, 2> a, b; // a <= b lexicographically
        double minX, minY, maxX, maxY;
    };
    const TerrainSculptData &data_;
    std::vector<Segment> segments_;
    std::array<double, 4> box_{};
    double reach_{}; // radius or width
    // Region: horizontal bands of edges for the winding test (at most terrainSculptBandEdges
    // edges a band for a valid region), and a bounding-volume tree of edges for distances.
    struct Node {
        double minX, minY, maxX, maxY;
        std::uint32_t first, count; // a leaf's edges in order_ (count 0: an inner node)
        std::uint32_t left, right;
    };
    double bandLow_{}, bandHeight_{};
    std::vector<std::vector<std::uint32_t>> bands_;
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> order_;
    std::uint32_t build(std::uint32_t first, std::uint32_t count);
};
// A region's horizontal bands (ADR 0169, R096.b notes): min(4,096, corners) bands of equal height
// over its Y range, each listing the edges whose Y range meets it. A position's winding test reads
// only its band, so the busiest band bounds the work per vertex; a region whose busiest band holds
// more than terrainSculptBandEdges edges (an outline crossing one horizontal line hundreds of
// times, such as a comb) is refused with LIMIT_EXCEEDED, located at that band.
inline constexpr std::size_t terrainSculptBandEdges = 512;
struct TerrainSculptBands {
    std::size_t count{};          // bands
    double low{}, height{};       // the first band's lower Y and the band height
    std::size_t busiest{}, edges{}; // the busiest band and its edge count
};
TerrainSculptBands terrainSculptRegionBands(const std::vector<std::array<double, 2>> &region);
// The band of a Y (clamped to the bands): monotone in Y.
std::size_t terrainSculptBandOf(const TerrainSculptBands &bands, double y);
// The plan box of an entry's influence (TerrainEditKind::bounds).
std::array<double, 4> terrainSculptBounds(const TerrainSculptData &data);

// The id of the `piece`th further run an entry splits off (0-based ordinal of the entry's
// block): 4,294,967,295 - (edit - 1) * 4,096 - piece, at or above terrainSplitSourceIdFloor,
// which base and added source ids stay below. Empty past terrainSculptPiecesPerEdit pieces or
// terrainSculptSplitEdits edits.
std::optional<std::uint32_t> terrainSplitSourceId(std::uint32_t edit, std::uint32_t piece);

// What an entry clips from the current sources (decision 2), computed without the mesh.
struct TerrainSculptClip {
    struct Source {
        std::uint32_t id{};
        TerrainSourceRole role{};
        std::vector<bool> keep;            // spot points: the points kept
        std::vector<TerrainSource> pieces; // lines: the runs kept (the first keeps the id)
    };
    std::vector<Source> sources; // touched sources, in current-source order
    std::uint64_t vertices{};    // source vertices clipped
    std::uint64_t pieces{};      // further runs (new source ids)
    std::uint64_t removed{};     // sources left with nothing
    std::vector<std::pair<std::uint32_t, std::uint32_t>> resolved; // (parent, piece id) made
    std::string refusal;         // non-empty when the entry may not split this much
};
// R096.d: the clip of the current sources by any rule (a sculpt's weight, a stamp's daylight
// polygon). Every contour, breakline and spot-point vertex in `box` that `vertex` marks is
// clipped, and every contour or breakline segment between two kept vertices that `segment` (when
// set) marks is cut, so the line splits there. A line keeps its id for its first run; further runs
// take the entry's recorded `pieces` for that line first, then fresh ids of the entry's block from
// ordinal `firstOrdinal` on (terrainSplitSourceId). Boundary and hole sources are never clipped.
struct TerrainSourceClipRule {
    std::array<double, 4> box{};
    std::function<bool(const Vec3 &)> vertex;
    std::function<bool(const Vec3 &, const Vec3 &)> segment;
    std::uint32_t firstOrdinal{};
    std::string tooManyPieces{"This change would split source lines into more than 4,096 pieces"};
};
TerrainSculptClip planTerrainSourceClip(const TerrainSourceState &state, std::uint32_t edit,
                                        const std::vector<std::pair<std::uint32_t, std::uint32_t>> &pieces,
                                        const TerrainSourceClipRule &rule,
                                        TerrainProgress *progress = nullptr);
// With `progress` (a replay), cancellation is checked part-way through the scan.
TerrainSculptClip planTerrainSculptClip(const TerrainSourceState &state, const TerrainEdit &edit,
                                        TerrainProgress *progress = nullptr);
// Applies a clip (the kind's `sources` hook): nothing when it is refused.
void applyTerrainSculptClip(TerrainSourceState &state, const TerrainSculptClip &clip);

// The change of an entry against the mesh before it.
struct TerrainSculptPlan {
    TerrainRegionChange change;      // lifts only, by position
    std::size_t influenced{};        // vertices with a weight above zero
    std::size_t protectedVertices{}; // of them, on or inside a pad (not moved)
    TerrainSculptClip clip;
    std::string refusal;             // why it does not apply (no change then)
    std::string refusalCode;         // INVALID_REQUEST or LIMIT_EXCEEDED
};
TerrainSculptPlan planTerrainSculpt(const TerrainEditContext &context, const TerrainEdit &edit);

// ---- R096.f1: the desktop preview (ADR 0169 §5) ---------------------------------------------
// One sculpt or smooth planned on the document's cached edit mesh and current sources, with its
// candidate tiles (previewTerrainEdit), nothing validated or committed. `data` is in the terrain
// frame, without recorded pieces (the plan resolves them). When the change does not apply,
// `plan.refusal` says why and `edit` is empty. Without `candidate` (a hover: the brush shows the
// vertices that will move but no surface), only the plan is made and the tiles it touches are
// counted. Refuses (TerrainError) a missing, locked, tilted or scaled terrain before planning,
// and a full edit list when the candidate is built.
struct TerrainSculptPreview {
    std::string kind;
    std::shared_ptr<const TerrainSculptData> data; // as given (no pieces)
    TerrainSculptPlan plan;
    std::optional<TerrainEditPreview> edit;        // the candidate when the change applies
    // The vertices that move: each position before the change (terrain frame) with its weight,
    // in the plan's (x, y) order. A smooth's lifted vertices only.
    std::vector<std::pair<Vec3, double>> moving;
    std::size_t touched{}; // tiles with a moving vertex (the candidate's replaced tiles)
    std::size_t tilesTouched() const { return edit ? edit->region.replaced.size() : touched; }
};
TerrainSculptPreview previewTerrainSculpt(const Document &doc, Id terrain, const std::string &kind,
                                          std::shared_ptr<const TerrainSculptData> data,
                                          bool candidate = true);
// Refuses (TerrainError INVALID_OPERATION) a terrain whose frame scales plan or height: sculpt and
// smooth store radius, width and amount in model metres.
void requireUnscaledTerrain(const Document &doc, Id terrain);
// The plan of the last candidate preview (one slot, thread-safe), when it was made for the same terrain
// record, the same cached edit mesh (the same tile records), kind and data: terrain.sculpt then
// commits it instead of planning again, so the committed tiles are the previewed ones (ADR 0169,
// R096.f notes "avoid planning twice"). Taking it clears the slot.
std::optional<TerrainSculptPlan> takeRememberedTerrainSculptPlan(const Document &doc, Id terrain,
                                                                 const std::string &kind,
                                                                 const TerrainSculptData &data);
// Clears the slot (tests, and a viewport whose preview ends without a commit).
void forgetRememberedTerrainSculptPlan();

// The two kinds (registered as built-in by core/terrain_edit.cpp).
std::vector<TerrainEditKind> terrainSculptKinds();
} // namespace sketchy
