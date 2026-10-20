#pragma once
// R096.c contour edit (ADR 0169 §4.2): the source edits `editSource`, `addSource` and
// `removeSource` as edit kinds, and the local re-triangulation they share.
//
// An entry stores its resolved geometry in the terrain frame: the source it names, the new role
// and points (editSource, addSource) exactly as the mesh holds them, and the plan box of the
// geometry it replaced (editSource, removeSource). Replay derives the old geometry from the
// current sources (core/terrain_sources.hpp), so an entry is small and a pure function of the
// mesh, the current sources and its own fields.
//
// The re-triangulation (planTerrainSourceEdit):
// - Elevation-only (the XY of every point unchanged): the changed vertices are lifted in Z and the
//   flat-triangle flip runs over the triangles incident to them (flipTerrainFlatTriangles).
// - Any XY change: the points are matched by exact plan position; vertices only the old geometry
//   has are removed (unless another current source uses them or they lie on the terrain's outline,
//   where they stay as plain vertices), vertices both have with a new Z are lifted, new ones are
//   inserted. The cavity is the triangles touching removed or lifted vertices, removed segments
//   (when their edge remains), inserted vertices and new segments, plus each inserted vertex's
//   Bowyer–Watson cavity (exact in-circle test, never across a constraint, the outline or a
//   protected region), closed to whole triangles. retriangulateTerrainCavity re-triangulates it
//   with the remaining constraints, and the result replaces the cavity (replaceTerrainRegion).
// - Validation: the crossing, duplicate and limit checks of the adapter against the current
//   sources inside the cavity (every current contour and breakline segment is a mesh edge, C1, so
//   any segment a new segment could cross is an edge of a triangle it touches); the outline rule
//   (decision 6: new geometry inside the terrain's outline and outside its holes, "outside-outline")
//   and the pad rule (geometry entering a protected region, "inside-pad"). A refusal changes
//   nothing and names its locations (TerrainEditRefused).
#include "core/terrain_edit.hpp"
#include "core/terrain_sources.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace sketchy {
inline constexpr const char *terrainEditSourceKind = "editSource";
inline constexpr const char *terrainAddSourceKind = "addSource";
inline constexpr const char *terrainRemoveSourceKind = "removeSource";
// Points per entry (ADR 0169 §7: the adapter's polygon bound).
inline constexpr std::size_t terrainSourceEditPoints = 20000;

struct TerrainSourceEditData final : TerrainEditData {
    std::uint32_t source{};                             // the source's id
    TerrainSourceRole role{TerrainSourceRole::Contour}; // editSource, addSource: the new role
    std::vector<Vec3> points;      // editSource, addSource: terrain frame; closed repeats its start
    std::array<double, 4> before{}; // editSource, removeSource: plan box of the old geometry
    bool equals(const TerrainEditData &other) const override;
    std::size_t bytes() const override;
};
// The fields a kind's entry must hold: a source id; for editSource and addSource a contour,
// breakline or points role with 2 (1 for points) to 20,000 finite points within the 1e6 m local
// bound, one elevation for a contour, and a closed polyline (last point repeats the first) of at
// least 4 points; for editSource and removeSource a finite box. Throws std::invalid_argument.
void validateTerrainSourceEditData(const std::string &kind, const TerrainSourceEditData &data);
// The id an added source takes: above every base source id, from the entry's own id (entry ids
// are never reused, so neither is a source id). Refuses (LIMIT_EXCEEDED) past the u32 range.
std::uint32_t terrainAddedSourceId(std::uint32_t maxBaseId, std::uint32_t edit);

// A refusal with located problems in the import's report kinds ("crossing-constraints",
// "conflicting-duplicate", "limit-exceeded", ...; R096.c adds "outside-outline" and "inside-pad").
// Locations are in the terrain frame. The code is LIMIT_EXCEEDED for limit kinds, otherwise
// INVALID_REQUEST.
struct TerrainEditProblem {
    std::string kind, message;
    std::uint64_t count{};
    std::vector<TerrainLocation> locations; // at most terrainMaxLocationsPerKind
};
class TerrainEditRefused : public TerrainError {
  public:
    TerrainEditRefused(std::string code, const std::string &message,
                       std::vector<TerrainEditProblem> problems)
        : TerrainError(std::move(code), message), problems_(std::move(problems)) {}
    const std::vector<TerrainEditProblem> &problems() const { return problems_; }

  private:
    std::vector<TerrainEditProblem> problems_;
};

struct TerrainSourceEditPlan {
    TerrainRegionChange change;
    std::vector<Vec3> resolved; // the new points exactly as the mesh holds them
    bool elevationOnly{};
    std::size_t cavityTriangles{}, addedTriangles{}, liftedVertices{}, removedVertices{},
        insertedVertices{};
    std::uint64_t mergedDuplicates{}, flips{}, flatRemaining{};
};
// The change replacing `before` (the current source, or null to add) by `after` (null to remove).
// Throws TerrainEditRefused.
TerrainSourceEditPlan planTerrainSourceEdit(const TerrainEditContext &context,
                                            const TerrainSource *before,
                                            const TerrainSource *after);

// Promotion of a display contour (ADR 0169 §4.2): the surface's contour line at `level` (terrain
// frame), as one polyline at that elevation. The surface is cut by the plane with a vertex at the
// level counted as above it, so the lines are simple; each crossing point is computed from the
// edge's endpoints in a canonical order (an endpoint at the level is that vertex exactly). Open
// lines end on the outline; their end points there are dropped unless they are mesh vertices, so
// the contour stays strictly inside the outline. With several lines at the level, `near` (terrain
// frame) chooses the nearest; without it the request is refused (INVALID_REQUEST). No line at the
// level is NOT_FOUND. A closed line repeats its start.
std::vector<Vec3> terrainContourAtLevel(const TerrainEditMesh &mesh, double level,
                                        std::optional<std::array<double, 2>> near);

// The three kinds (registered as built-in by core/terrain_edit.cpp).
std::vector<TerrainEditKind> terrainSourceEditKinds();
} // namespace sketchy
