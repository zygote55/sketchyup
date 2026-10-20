#pragma once
// R096.c current sources (ADR 0169 §2, hybrid model (c)): the base sources of a terrain record
// with every source edit of its edit list applied in order, derived without a mesh and without
// triangulating. Contour edits validate against them and recover the constraint segments of a
// cavity from them; terrain.describe reports them; check C1 compares them with the mesh.
//
// The state indexes, by exact plan position (+0.0 and -0.0 are one position), the sources that
// have a vertex there and the contour and breakline segments between two positions, so the
// edit core asks about a cavity's vertices and edges without scanning every source.
#include "core/terrain_records.hpp"
#include "geometry/terrain_triangulation.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace sketchy {
class Document;
struct TerrainEdit;
struct TerrainEditKind;
// R096.b: source ids at or above this are the further runs of lines a sculpt split
// (terrainSplitSourceId); base and added source ids stay below it.
inline constexpr std::uint32_t terrainSplitSourceIdFloor = 3221225472u; // 2^32 - 2^30
// An exact plan position.
struct TerrainPlanKey {
    std::uint64_t x{}, y{};
    bool operator==(const TerrainPlanKey &) const = default;
    auto operator<=>(const TerrainPlanKey &) const = default;
    static TerrainPlanKey of(double x, double y);
};
struct TerrainPlanHash {
    std::size_t operator()(const TerrainPlanKey &key) const;
};
// A plan polygon an entry protects (R096.d's pads), with the entry that made it.
struct TerrainProtectedRegion {
    std::uint32_t edit{};
    std::vector<std::array<double, 2>> polygon;
};
class TerrainSourceState {
  public:
    TerrainSourceState() = default;
    explicit TerrainSourceState(const TerrainSources &base);
    // The current sources: the base sources in their order, edited in place, then the added
    // ones in the order they were added; removed ones are gone.
    std::vector<const TerrainSource *> sources() const;
    std::size_t size() const { return byId_.size(); }
    const TerrainSource *find(std::uint32_t id) const;
    // The largest base source id (added sources take ids above it, see terrainAddedSourceId).
    std::uint32_t maxBaseId() const { return maxBaseId_; }
    std::size_t vertexCount() const { return vertices_; }
    // Mutators (source edits). `add` refuses an id in use and `replace` / `remove` an unknown
    // one (std::invalid_argument); the derivation skips such entries instead.
    void add(TerrainSource source);
    void replace(TerrainSource source);
    void remove(std::uint32_t id);
    // R096.b: keeps the spot points of source `id` that `keep` marks (one flag per point) and
    // drops the source when none is left, updating the index for the dropped positions only, so
    // a sculpt over a large point source does not re-index all of it. Refuses an unknown id, a
    // source that is not spot points or a mask of another length (std::invalid_argument).
    void clipPoints(std::uint32_t id, const std::vector<bool> &keep);
    // Index: how many current sources have a vertex at exactly (x, y), of any role or of the
    // contour role; and how many contour or breakline sources have the segment between two
    // positions (either direction). A source counts once per position or segment.
    std::uint32_t vertexUses(TerrainPlanKey at) const;
    std::uint32_t contourUses(TerrainPlanKey at) const;
    std::uint32_t segmentUses(TerrainPlanKey a, TerrainPlanKey b) const;
    std::vector<TerrainProtectedRegion> protectedRegions;

  private:
    struct Uses {
        std::uint32_t all{}, contour{};
    };
    struct SegmentKey {
        TerrainPlanKey a, b;
        bool operator==(const SegmentKey &) const = default;
    };
    struct SegmentHash {
        std::size_t operator()(const SegmentKey &key) const;
    };
    void index(const TerrainSource &source, int sign);
    std::vector<std::shared_ptr<const TerrainSource>> slots_; // null where removed
    std::unordered_map<std::uint32_t, std::size_t> byId_;
    std::unordered_map<TerrainPlanKey, Uses, TerrainPlanHash> positions_;
    std::unordered_map<SegmentKey, std::uint32_t, SegmentHash> segments_;
    std::uint32_t maxBaseId_{};
    std::size_t vertices_{};
};
// The polyline segments of a source by exact plan position (contours and breaklines; a source
// whose last point repeats its first is closed), zero-length ones left out, each once.
std::vector<std::array<TerrainPlanKey, 2>> terrainSourceSegments(const TerrainSource &source);
// One entry's effect on the current sources: the kind's `sources` hook, then its `protects`
// polygons. An entry that does not fit the state (an unknown or duplicate source id) changes
// nothing; the derivation never throws for it.
void applyTerrainSourceEffect(TerrainSourceState &state, const TerrainEditKind &kind,
                              const TerrainEdit &edit);
// The current sources of a record, cached: a record whose edit list extends a cached one by
// further entries is derived from it (in place when nobody else holds it), so successive edits
// and previews do not rebuild the index. Thread-safe.
std::shared_ptr<const TerrainSourceState> terrainSourceState(const TerrainRecord &record);
void clearTerrainSourceStateCache();

// ---- Cleaning (shared with the importer, ADR 0168 §1) --------------------------------------
// Removes zero-length segments (a vertex within 1e-7 m in x and y and 1e-9 m in z of its
// predecessor) and collinear spikes (a vertex where the polyline doubles back exactly along
// itself), counting both; a closed polyline (the closing edge implied, its start not repeated)
// is cleaned across its start too.
struct TerrainPolylineCleaning {
    std::uint64_t zeroLength{}, spikes{};
};
void cleanTerrainPolyline(std::vector<TerrainVec3> &points, bool closed,
                          TerrainPolylineCleaning &counts);
// Canonical order: a ring starts at its lexicographically smallest vertex and runs
// counter-clockwise; an open polyline starts at its smaller endpoint.
void canonicalTerrainPolyline(std::vector<TerrainVec3> &points, bool closed);

// ---- Check C1 (ADR 0169 §6) ------------------------------------------------------------------
// Every current contour and breakline vertex is a mesh vertex (an exact position, Z bit-equal to
// its stored value) and every segment of theirs is a mesh edge. `problems` names at most 32.
struct TerrainSourceConsistency {
    std::size_t sources{}, vertices{}, segments{};
    std::size_t missingVertices{}, missingSegments{};
    std::vector<std::string> problems;
    bool ok() const { return !missingVertices && !missingSegments; }
};
// C1 covers the base sources and the entries that applied: the entries a replay skipped
// (`skipped`, by id, from its edit-skipped notices) are left out of the derivation, since the
// mesh does not hold their change.
TerrainSourceConsistency checkTerrainSourceConsistency(const Document &doc, Id terrain,
                                                       const std::vector<std::uint32_t> &skipped = {});
} // namespace sketchy
