#include "core/terrain_source_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
namespace sketchy {
namespace {
using Corners = std::array<Vec3, 3>;
constexpr double localBound = 1e6;
constexpr double xyTolerance = 1e-7;

TerrainVec2 v2(const Vec3 &p) { return {p.x, p.y}; }
TerrainVec2 v2(const std::array<double, 2> &p) { return {p[0], p[1]}; }
int orient(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c) { return terrainOrientation(a, b, c); }
TerrainPlanKey keyOf(const Vec3 &p) { return TerrainPlanKey::of(p.x, p.y); }
const char *roleText(TerrainSourceRole role) {
    switch (role) {
    case TerrainSourceRole::Contour:
        return "contour";
    case TerrainSourceRole::Breakline:
        return "breakline";
    case TerrainSourceRole::Boundary:
        return "boundary";
    case TerrainSourceRole::Hole:
        return "hole";
    case TerrainSourceRole::Points:
        return "points";
    }
    return "source";
}

// Closed plan geometry with the adapter's exact orientation.
bool inTriangle(TerrainVec2 a, TerrainVec2 b, TerrainVec2 c, TerrainVec2 p) {
    return orient(a, b, p) >= 0 && orient(b, c, p) >= 0 && orient(c, a, p) >= 0;
}
bool segmentsMeet(TerrainVec2 p, TerrainVec2 q, TerrainVec2 a, TerrainVec2 b) {
    const int o1 = orient(p, q, a), o2 = orient(p, q, b), o3 = orient(a, b, p),
              o4 = orient(a, b, q);
    if (o1 * o2 > 0 || o3 * o4 > 0)
        return false;
    if (o1 == 0 && o2 == 0) // collinear: the projections must overlap
        return std::max(std::min(p.x, q.x), std::min(a.x, b.x)) <=
                   std::min(std::max(p.x, q.x), std::max(a.x, b.x)) &&
               std::max(std::min(p.y, q.y), std::min(a.y, b.y)) <=
                   std::min(std::max(p.y, q.y), std::max(a.y, b.y));
    return true;
}
TerrainVec2 crossingPoint(TerrainVec2 p, TerrainVec2 q, TerrainVec2 a, TerrainVec2 b) {
    const double d = (q.x - p.x) * (b.y - a.y) - (q.y - p.y) * (b.x - a.x);
    if (d == 0)
        return a;
    const double t = ((a.x - p.x) * (b.y - a.y) - (a.y - p.y) * (b.x - a.x)) / d;
    return {p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)};
}
// Whether two collinear segments share more than a point (exact).
bool overlapAlong(TerrainVec2 p, TerrainVec2 q, TerrainVec2 a, TerrainVec2 b) {
    if (orient(p, q, a) != 0 || orient(p, q, b) != 0)
        return false;
    const bool alongX = std::abs(q.x - p.x) >= std::abs(q.y - p.y);
    const auto lo = [&](TerrainVec2 u, TerrainVec2 v) { return alongX ? std::min(u.x, v.x) : std::min(u.y, v.y); };
    const auto hi = [&](TerrainVec2 u, TerrainVec2 v) { return alongX ? std::max(u.x, v.x) : std::max(u.y, v.y); };
    return std::max(lo(p, q), lo(a, b)) < std::min(hi(p, q), hi(a, b));
}
// Strictly between the ends of a segment it is collinear with.
bool strictlyInside(TerrainVec2 p, TerrainVec2 q, TerrainVec2 v) {
    if (orient(p, q, v) != 0 || (v.x == p.x && v.y == p.y) || (v.x == q.x && v.y == q.y))
        return false;
    return std::min(p.x, q.x) <= v.x && v.x <= std::max(p.x, q.x) && std::min(p.y, q.y) <= v.y &&
           v.y <= std::max(p.y, q.y);
}
// Closed point-in-polygon by winding number (exact), and the polygon's closed region meeting a
// segment.
bool inPolygon(const TerrainPlanPolygon &polygon, TerrainVec2 q) {
    const std::size_t n = polygon.size();
    int winding = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto a = v2(polygon[i]), b = v2(polygon[(i + 1) % n]);
        const int o = orient(a, b, q);
        if (o == 0 && std::min(a.x, b.x) <= q.x && q.x <= std::max(a.x, b.x) &&
            std::min(a.y, b.y) <= q.y && q.y <= std::max(a.y, b.y))
            return true;
        if (a.y <= q.y) {
            if (b.y > q.y && o > 0)
                ++winding;
        } else if (b.y <= q.y && o < 0)
            --winding;
    }
    return winding != 0;
}
bool polygonMeetsSegment(const TerrainPlanPolygon &polygon, TerrainVec2 p, TerrainVec2 q) {
    if (inPolygon(polygon, p) || inPolygon(polygon, q))
        return true;
    for (std::size_t i = 0; i < polygon.size(); ++i)
        if (segmentsMeet(p, q, v2(polygon[i]), v2(polygon[(i + 1) % polygon.size()])))
            return true;
    return false;
}

// ---- Problems -------------------------------------------------------------------------------
class Problems {
  public:
    void add(const std::string &kind, const std::string &message, TerrainVec2 xy,
             std::uint64_t a = 0, std::uint64_t b = 0) {
        auto &entry = find(kind, message);
        ++entry.count;
        if (entry.locations.size() < terrainMaxLocationsPerKind)
            entry.locations.push_back({xy, a, b});
    }
    void merge(const TerrainProblem &problem) {
        auto &entry = find(terrainProblemKindName(problem.kind), problem.message);
        entry.count += problem.count;
        for (const auto &location : problem.locations)
            if (entry.locations.size() < terrainMaxLocationsPerKind)
                entry.locations.push_back(location);
    }
    bool any() const { return !list_.empty(); }
    [[noreturn]] void refuse(const std::string &what) const {
        bool limit = false;
        std::string kinds;
        for (const auto &problem : list_) {
            limit = limit || problem.kind == "limit-exceeded" || problem.kind == "triangle-budget" ||
                    problem.kind == "extent-exceeded";
            if (!kinds.empty())
                kinds += ", ";
            kinds += problem.kind + " (" + std::to_string(problem.count) + ")";
        }
        throw TerrainEditRefused(limit ? "LIMIT_EXCEEDED" : "INVALID_REQUEST",
                                 (what + " refused; nothing was changed: " + kinds).substr(0, 256),
                                 list_);
    }
    void check(const std::string &what) const {
        if (any())
            refuse(what);
    }

  private:
    TerrainEditProblem &find(const std::string &kind, const std::string &message) {
        for (auto &problem : list_)
            if (problem.kind == kind)
                return problem;
        list_.push_back({kind, message.substr(0, terrainMaxMessageBytes), 0, {}});
        return list_.back();
    }
    std::vector<TerrainEditProblem> list_;
};
constexpr const char *outsideOutline = "outside-outline";
constexpr const char *outsideMessage =
    "Terrain edits stay inside the terrain's outline and outside its holes.";
constexpr const char *insidePad = "inside-pad";
constexpr const char *padMessage = "A building pad protects this area; remove its stamp first.";
constexpr const char *conflictKind = "conflicting-duplicate";

// ---- The mesh, by exact position ------------------------------------------------------------
class View {
  public:
    explicit View(const TerrainEditMesh &mesh) : mesh_(mesh) {}
    const TerrainEditMesh &mesh() const { return mesh_; }
    Corners corners(const TerrainTriangleRef &ref) const {
        const auto &content = *tile(ref.tile).content;
        const auto &t = content.triangles[ref.triangle];
        return {content.vertices[t[0]], content.vertices[t[1]], content.vertices[t[2]]};
    }
    const std::vector<TerrainTriangleRef> &at(const Vec3 &p) const {
        const auto key = TerrainPositionKey::of(p);
        auto found = incident_.find(key);
        if (found == incident_.end())
            found = incident_.emplace(key, mesh_.trianglesAt(p)).first;
        return found->second;
    }
    // The triangles with both corners: one or two (an edge), none when it is not an edge.
    std::vector<TerrainTriangleRef> edge(const Vec3 &a, const Vec3 &b) const {
        const auto &l = at(a), &r = at(b);
        std::vector<TerrainTriangleRef> shared;
        std::set_intersection(l.begin(), l.end(), r.begin(), r.end(), std::back_inserter(shared));
        return shared;
    }
    bool boundaryEdge(const Vec3 &a, const Vec3 &b) const { return edge(a, b).size() == 1; }
    bool boundaryVertex(const Vec3 &p) const {
        for (const auto &ref : at(p)) {
            const auto c = corners(ref);
            for (int i = 0; i < 3; ++i)
                if (c[std::size_t(i)] == p && (boundaryEdge(p, c[std::size_t((i + 1) % 3)]) ||
                                               boundaryEdge(p, c[std::size_t((i + 2) % 3)])))
                    return true;
        }
        return false;
    }
    std::optional<TerrainTriangleRef> across(const TerrainTriangleRef &from, const Vec3 &a,
                                             const Vec3 &b) const {
        for (const auto &ref : edge(a, b))
            if (!(ref == from))
                return ref;
        return std::nullopt;
    }
    // Triangles whose closed region contains (x, y), in reference order.
    std::vector<TerrainTriangleRef> containing(TerrainVec2 p) const {
        std::vector<TerrainTriangleRef> result;
        for (const auto *tile : mesh_.tilesAt(p.x, p.y, 0)) {
            const auto &content = *tile->content;
            for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
                const auto &tri = content.triangles[t];
                const auto a = v2(content.vertices[tri[0]]), b = v2(content.vertices[tri[1]]),
                           c = v2(content.vertices[tri[2]]);
                if (p.x < std::min({a.x, b.x, c.x}) || p.x > std::max({a.x, b.x, c.x}) ||
                    p.y < std::min({a.y, b.y, c.y}) || p.y > std::max({a.y, b.y, c.y}))
                    continue;
                if (inTriangle(a, b, c, p))
                    result.push_back({content.key, t});
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }
    // Triangles whose closed region meets the closed segment, in reference order. The tiles come
    // from the grid index along the segment (a box of at most one cell per step), and a triangle
    // wholly more than 1e-6 m to one side of the segment's line is skipped before the exact test.
    std::vector<TerrainTriangleRef> touching(TerrainVec2 p, TerrainVec2 q) const {
        const double cell = mesh_.grid().cell > 0 ? mesh_.grid().cell : 1.0;
        const double dx = q.x - p.x, dy = q.y - p.y;
        const double length = std::hypot(dx, dy);
        const auto steps = std::size_t(std::clamp(std::ceil(length / cell), 1.0, 1e6));
        std::vector<const TerrainEditMesh::Tile *> tiles;
        for (std::size_t i = 0; i < steps; ++i) {
            const double t0 = double(i) / double(steps), t1 = double(i + 1) / double(steps);
            const double x0 = p.x + dx * t0, y0 = p.y + dy * t0;
            const double x1 = i + 1 == steps ? q.x : p.x + dx * t1,
                         y1 = i + 1 == steps ? q.y : p.y + dy * t1;
            for (const auto *tile : mesh_.tilesIn(std::min(x0, x1), std::min(y0, y1),
                                                  std::max(x0, x1), std::max(y0, y1), 1e-6))
                tiles.push_back(tile);
        }
        std::sort(tiles.begin(), tiles.end(), [](const auto *a, const auto *b) {
            return a->content->key < b->content->key;
        });
        tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
        const double minX = std::min(p.x, q.x), maxX = std::max(p.x, q.x),
                     minY = std::min(p.y, q.y), maxY = std::max(p.y, q.y);
        const double margin = 1e-6 * (std::abs(dx) + std::abs(dy));
        std::vector<TerrainTriangleRef> result;
        for (const auto *tile : tiles) {
            const auto &content = *tile->content;
            for (std::uint32_t t = 0; t < content.triangles.size(); ++t) {
                const auto &tri = content.triangles[t];
                const auto a = v2(content.vertices[tri[0]]), b = v2(content.vertices[tri[1]]),
                           c = v2(content.vertices[tri[2]]);
                if (maxX < std::min({a.x, b.x, c.x}) || minX > std::max({a.x, b.x, c.x}) ||
                    maxY < std::min({a.y, b.y, c.y}) || minY > std::max({a.y, b.y, c.y}))
                    continue;
                const auto side = [&](TerrainVec2 v) { return dx * (v.y - p.y) - dy * (v.x - p.x); };
                const double sa = side(a), sb = side(b), sc = side(c);
                if ((sa > margin && sb > margin && sc > margin) ||
                    (sa < -margin && sb < -margin && sc < -margin))
                    continue;
                if (inTriangle(a, b, c, p) || inTriangle(a, b, c, q) || segmentsMeet(p, q, a, b) ||
                    segmentsMeet(p, q, b, c) || segmentsMeet(p, q, c, a))
                    result.push_back({content.key, t});
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

  private:
    const TerrainEditMesh::Tile &tile(const TerrainTileKey &key) const {
        if (last_ && last_->content->key == key)
            return *last_;
        const auto *found = mesh_.tile(key);
        if (!found)
            throw std::logic_error("A terrain triangle names a missing tile");
        last_ = found;
        return *found;
    }
    const TerrainEditMesh &mesh_;
    mutable const TerrainEditMesh::Tile *last_{};
    mutable std::unordered_map<TerrainPositionKey, std::vector<TerrainTriangleRef>,
                               TerrainPositionHash>
        incident_;
};

TerrainTriangulationOptions adapterOptions(const TerrainParameters &parameters) {
    TerrainTriangulationOptions options;
    options.flatMode =
        parameters.flipFlatTriangles ? TerrainFlatMode::Flip : TerrainFlatMode::ReportOnly;
    return options;
}

struct SourceView {
    const TerrainSource *source;
    std::map<TerrainPlanKey, Vec3> at; // first point at each position
    std::vector<std::array<TerrainPlanKey, 2>> segments;
    bool contour{};
    explicit SourceView(const TerrainSource *s) : source(s) {
        if (!s)
            return;
        contour = s->role == TerrainSourceRole::Contour;
        for (const auto &p : s->points)
            at.try_emplace(keyOf(p), p);
        segments = terrainSourceSegments(*s);
    }
    bool has(TerrainPlanKey key) const { return at.contains(key); }
    bool hasSegment(TerrainPlanKey a, TerrainPlanKey b) const {
        if (b < a)
            std::swap(a, b);
        return std::binary_search(segments.begin(), segments.end(),
                                  std::array<TerrainPlanKey, 2>{a, b});
    }
};

// The current sources after the edit, by position, without building them.
struct After {
    const TerrainSourceState &state;
    const SourceView &before, &after;
    std::uint32_t uses(TerrainPlanKey key) const {
        return state.vertexUses(key) - (before.has(key) ? 1 : 0) + (after.has(key) ? 1 : 0);
    }
    std::uint32_t others(TerrainPlanKey key) const {
        return state.vertexUses(key) - (before.has(key) ? 1 : 0);
    }
    bool contour(TerrainPlanKey key) const {
        return state.contourUses(key) - (before.contour && before.has(key) ? 1 : 0) +
                   (after.contour && after.has(key) ? 1 : 0) >
               0;
    }
    bool constraint(TerrainPlanKey a, TerrainPlanKey b) const {
        return state.segmentUses(a, b) - (before.hasSegment(a, b) ? 1 : 0) +
                   (after.hasSegment(a, b) ? 1 : 0) >
               0;
    }
};

void checkProtected(const TerrainSourceState &state, const std::vector<Vec3> &points,
                    const std::vector<std::array<Vec3, 2>> &segments, Problems &problems,
                    std::uint64_t source) {
    for (const auto &region : state.protectedRegions) {
        if (region.polygon.size() < 3)
            continue;
        for (const auto &p : points)
            if (inPolygon(region.polygon, v2(p)))
                problems.add(insidePad, padMessage, v2(p), source, region.edit);
        for (const auto &[a, b] : segments)
            if (polygonMeetsSegment(region.polygon, v2(a), v2(b)))
                problems.add(insidePad, padMessage,
                             {(a.x + b.x) / 2, (a.y + b.y) / 2}, source, region.edit);
    }
}
bool protectedAt(const TerrainSourceState &state, TerrainVec2 p) {
    for (const auto &region : state.protectedRegions)
        if (region.polygon.size() >= 3 && inPolygon(region.polygon, p))
            return true;
    return false;
}

// ---- Elevation only -------------------------------------------------------------------------
TerrainSourceEditPlan planElevation(const TerrainEditContext &context, const TerrainSource &old,
                                    const TerrainSource &next, const SourceView &before,
                                    const SourceView &after) {
    View view(context.mesh);
    const After now{context.sources, before, after};
    Problems problems;
    TerrainSourceEditPlan plan;
    plan.elevationOnly = true;
    plan.resolved = next.points;
    std::map<TerrainPlanKey, std::pair<Vec3, double>> lifts;
    for (std::size_t i = 0; i < old.points.size(); ++i) {
        const auto &p = old.points[i], &q = next.points[i];
        if (p.z == q.z)
            continue;
        const auto key = keyOf(p);
        if (const auto found = lifts.find(key); found != lifts.end()) {
            if (found->second.second != q.z)
                problems.add(conflictKind, "A position takes two elevations.", v2(p), next.id);
            continue;
        }
        if (now.others(key))
            problems.add(conflictKind,
                         "A vertex another source shares cannot take a new elevation.", v2(p),
                         next.id);
        else if (view.at(p).empty())
            problems.add("triangulation-failed", "A source vertex is not a mesh vertex.", v2(p),
                         next.id);
        lifts.emplace(key, std::pair{p, q.z});
    }
    {
        std::vector<Vec3> moved;
        for (const auto &[key, lift] : lifts)
            moved.push_back(lift.first);
        checkProtected(context.sources, moved, {}, problems, next.id);
    }
    problems.check("Contour edit");
    // The flip region: the triangles incident to the lifted vertices, and to every vertex of the
    // source when its role changes (the flat rule counts contour vertices).
    std::set<TerrainTriangleRef> region;
    for (const auto &[key, lift] : lifts)
        for (const auto &ref : view.at(lift.first))
            region.insert(ref);
    if (old.role != next.role)
        for (const auto &p : old.points)
            for (const auto &ref : view.at(p))
                region.insert(ref);
    plan.liftedVertices = lifts.size();
    auto liftedPosition = [&](Vec3 p) {
        if (const auto found = lifts.find(keyOf(p)); found != lifts.end())
            p.z = found->second.second;
        return p;
    };
    if (region.empty() || !context.parameters.flipFlatTriangles) {
        for (const auto &[key, lift] : lifts)
            plan.change.lifted.push_back({lift.first, lift.second});
        return plan;
    }
    TerrainFlipInput input;
    std::map<TerrainPlanKey, std::uint32_t> index;
    std::vector<Vec3> positions;
    for (const auto &ref : region) {
        const auto c = view.corners(ref);
        std::array<std::uint32_t, 3> tri{};
        for (int i = 0; i < 3; ++i) {
            const auto p = liftedPosition(c[std::size_t(i)]);
            const auto key = keyOf(p);
            const auto [it, inserted] = index.try_emplace(key, std::uint32_t(positions.size()));
            if (inserted) {
                positions.push_back(p);
                input.vertices.push_back({{p.x, p.y, p.z}, now.contour(key), next.id});
            }
            tri[std::size_t(i)] = it->second;
        }
        input.triangles.push_back(tri);
    }
    for (const auto &tri : input.triangles)
        for (int i = 0; i < 3; ++i) {
            const auto a = tri[std::size_t(i)], b = tri[std::size_t((i + 1) % 3)];
            if (a < b && now.constraint(keyOf(positions[a]), keyOf(positions[b])))
                input.constraints.push_back({a, b});
        }
    const auto flipped = flipTerrainFlatTriangles(input, adapterOptions(context.parameters));
    if (!flipped.ok) {
        for (const auto &problem : flipped.report.refusals)
            problems.merge(problem);
        problems.refuse("Contour edit");
    }
    plan.flips = flipped.report.flipsApplied;
    plan.flatRemaining = flipped.report.flatTrianglesRemaining;
    if (!plan.flips) {
        for (const auto &[key, lift] : lifts)
            plan.change.lifted.push_back({lift.first, lift.second});
        return plan;
    }
    plan.change.removed.assign(region.begin(), region.end());
    for (const auto &t : flipped.triangles)
        plan.change.added.push_back({positions[t[0]], positions[t[1]], positions[t[2]]});
    plan.cavityTriangles = region.size();
    plan.addedTriangles = plan.change.added.size();
    return plan;
}
} // namespace

// ---- Planning -------------------------------------------------------------------------------
TerrainSourceEditPlan planTerrainSourceEdit(const TerrainEditContext &context,
                                            const TerrainSource *old, const TerrainSource *next) {
    const SourceView before(old), after(next);
    const auto &state = context.sources;
    const After now{state, before, after};
    const std::uint64_t sourceId = next ? next->id : old ? old->id : 0;
    // Elevation-only: the same plan positions in order and the same segments (a role change
    // between spot points and a polyline adds or drops segments, so it re-triangulates).
    if (old && next && old->points.size() == next->points.size() &&
        before.segments == after.segments) {
        bool sameXy = true;
        for (std::size_t i = 0; i < old->points.size() && sameXy; ++i)
            sameXy = keyOf(old->points[i]) == keyOf(next->points[i]);
        if (sameXy)
            return planElevation(context, *old, *next, before, after);
    }
    View view(context.mesh);
    Problems problems;
    TerrainSourceEditPlan plan;
    // ---- Vertices by exact plan position ----
    if (next) {
        std::map<TerrainPlanKey, double> seen;
        for (const auto &p : next->points)
            if (const auto [it, inserted] = seen.try_emplace(keyOf(p), p.z);
                !inserted && it->second != p.z)
                problems.add(conflictKind, "A position takes two elevations.", v2(p), sourceId);
    }
    std::map<TerrainPlanKey, Vec3> removed;   // old position
    std::map<TerrainPlanKey, Vec3> lifted;    // old position; new Z from `after`
    std::map<TerrainPlanKey, Vec3> inserted;  // new position
    std::map<TerrainPlanKey, Vec3> joined;    // new position equal to a kept mesh vertex
    for (const auto &[key, p] : before.at) {
        if (const auto found = after.at.find(key); found != after.at.end()) {
            if (found->second.z == p.z)
                continue;
            if (now.others(key))
                problems.add(conflictKind,
                             "A vertex another source shares cannot take a new elevation.", v2(p),
                             sourceId);
            else if (view.at(p).empty())
                inserted.emplace(key, found->second);
            else
                lifted.emplace(key, p); // keeps its place (on the outline too), with the new Z
            continue;
        }
        if (now.others(key) || view.at(p).empty() || view.boundaryVertex(p))
            continue; // another source's vertex, not on the mesh, or on the outline: it stays
        removed.emplace(key, p);
    }
    std::set<TerrainTriangleRef> cavity;
    // The cavity is bounded while it is collected, so an edit that would re-triangulate much of
    // the terrain is refused early (and skipped early on replay).
    const auto grow = [&](const TerrainTriangleRef &ref, TerrainVec2 where) {
        if (cavity.insert(ref).second && cavity.size() > terrainSourceEditCavityTriangles) {
            problems.add("limit-exceeded",
                         "The change would re-triangulate more than " +
                             std::to_string(terrainSourceEditCavityTriangles) +
                             " triangles; split it into smaller changes.",
                         where, sourceId);
            problems.refuse("Contour edit");
        }
    };
    std::size_t touched = 0;
    const auto touch = [&](std::size_t count, TerrainVec2 where) {
        touched += count;
        if (touched > terrainSourceEditTouchedTriangles) {
            problems.add("limit-exceeded",
                         "The change would re-triangulate too much of the terrain (its segments "
                         "touch over " +
                             std::to_string(terrainSourceEditTouchedTriangles) +
                             " triangles in all); split it into smaller changes.",
                         where, sourceId);
            problems.refuse("Contour edit");
        }
    };
    std::map<TerrainPlanKey, std::vector<TerrainTriangleRef>> containing; // inserted vertices
    for (const auto &[key, q] : after.at) {
        if (before.has(key) && !inserted.contains(key))
            continue;
        auto tris = view.containing(v2(q));
        if (tris.empty()) {
            problems.add(outsideOutline, outsideMessage, v2(q), sourceId);
            continue;
        }
        // An existing vertex at exactly this position.
        std::optional<Vec3> existing;
        for (const auto &ref : tris)
            for (const auto &c : view.corners(ref))
                if (keyOf(c) == key)
                    existing = c;
        if (existing) {
            if (existing->z == q.z && !removed.contains(key)) {
                joined.emplace(key, q);
                continue;
            }
            if (now.others(key) || view.boundaryVertex(*existing)) {
                problems.add(conflictKind,
                             "The position is a vertex of the surface with another elevation.",
                             v2(q), sourceId);
                continue;
            }
            removed.emplace(key, *existing); // a plain vertex the new one replaces
            tris = view.at(*existing);
        } else {
            // Strictly inside the outline: not on an outline edge.
            for (const auto &ref : tris) {
                const auto c = view.corners(ref);
                for (int i = 0; i < 3; ++i) {
                    const auto &a = c[std::size_t(i)], &b = c[std::size_t((i + 1) % 3)];
                    if (orient(v2(a), v2(b), v2(q)) == 0 && view.boundaryEdge(a, b))
                        problems.add(outsideOutline, outsideMessage, v2(q), sourceId);
                }
            }
        }
        inserted.emplace(key, q);
        touch(tris.size(), v2(q));
        for (const auto &ref : tris)
            grow(ref, v2(q));
        containing.emplace(key, std::move(tris));
    }
    // ---- Segments ----
    std::vector<std::array<Vec3, 2>> addedSegments, removedSegments;
    for (const auto &[a, b] : before.segments)
        if (!after.hasSegment(a, b))
            removedSegments.push_back({before.at.at(a), before.at.at(b)});
    for (const auto &[a, b] : after.segments)
        if (!before.hasSegment(a, b))
            addedSegments.push_back({after.at.at(a), after.at.at(b)});
    for (const auto &[a, b] : removedSegments) {
        if (now.constraint(keyOf(a), keyOf(b)))
            continue; // another source keeps the segment
        for (const auto &ref : view.edge(a, b))
            grow(ref, v2(a));
    }
    for (const auto &[a, b] : addedSegments) {
        const auto tris = view.touching(v2(a), v2(b));
        touch(tris.size(), v2(a));
        // Between two points of the outline a segment may run outside it without crossing an
        // outline edge (across a notch or a hole): its midpoint is then on no triangle.
        const TerrainVec2 middle{a.x + (b.x - a.x) / 2, a.y + (b.y - a.y) / 2};
        if (tris.empty() || view.containing(middle).empty())
            problems.add(outsideOutline, outsideMessage, middle, sourceId);
        const auto ka = keyOf(a), kb = keyOf(b);
        for (const auto &ref : tris) {
            grow(ref, v2(a));
            const auto c = view.corners(ref);
            for (int i = 0; i < 3; ++i) {
                const auto &u = c[std::size_t(i)], &w = c[std::size_t((i + 1) % 3)];
                const auto ku = keyOf(u), kw = keyOf(w);
                // A plain vertex (no current source's, not on the outline) exactly on the new
                // segment is removed with the cavity rather than refused as a touch.
                if (strictlyInside(v2(a), v2(b), v2(u)) && !now.uses(ku) && !removed.contains(ku) &&
                    !lifted.contains(ku) && !view.boundaryVertex(u))
                    removed.emplace(ku, u);
                const bool shared = ku == ka || ku == kb || kw == ka || kw == kb;
                if (!view.boundaryEdge(u, w))
                    continue;
                // The outline rule (§3): a new segment must not meet an outline or hole edge
                // except at its own ends, and must not run along one.
                if (shared ? overlapAlong(v2(a), v2(b), v2(u), v2(w))
                           : segmentsMeet(v2(a), v2(b), v2(u), v2(w)))
                    problems.add(outsideOutline, outsideMessage,
                                 shared ? middle : crossingPoint(v2(a), v2(b), v2(u), v2(w)),
                                 sourceId);
            }
        }
    }
    // ---- The pad rule ----
    {
        std::vector<Vec3> points;
        for (const auto &[key, p] : removed)
            points.push_back(p);
        for (const auto &[key, p] : lifted)
            points.push_back(p);
        for (const auto &[key, p] : inserted)
            points.push_back(p);
        auto segments = addedSegments;
        segments.insert(segments.end(), removedSegments.begin(), removedSegments.end());
        checkProtected(state, points, segments, problems, sourceId);
    }
    problems.check("Contour edit");
    // ---- The cavity ----
    for (const auto &[key, p] : removed)
        for (const auto &ref : view.at(p))
            grow(ref, v2(p));
    for (const auto &[key, p] : lifted)
        for (const auto &ref : view.at(p))
            grow(ref, v2(p));
    // Bowyer–Watson: from each inserted vertex's triangles, across edges that are not
    // constraints, the outline or a protected region, to triangles whose circumcircle holds it.
    for (const auto &[key, start] : containing) {
        const auto q = inserted.at(key);
        std::set<TerrainTriangleRef> visited(start.begin(), start.end());
        std::vector<TerrainTriangleRef> queue(start.begin(), start.end());
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const auto ref = queue[head];
            const auto c = view.corners(ref);
            for (int i = 0; i < 3; ++i) {
                const auto &a = c[std::size_t(i)], &b = c[std::size_t((i + 1) % 3)];
                if (now.constraint(keyOf(a), keyOf(b)))
                    continue;
                const auto next = view.across(ref, a, b);
                if (!next || visited.contains(*next))
                    continue;
                visited.insert(*next);
                const auto n = view.corners(*next);
                if (terrainInCircle(v2(n[0]), v2(n[1]), v2(n[2]), v2(q)) <= 0)
                    continue;
                if (protectedAt(state, {(n[0].x + n[1].x + n[2].x) / 3,
                                        (n[0].y + n[1].y + n[2].y) / 3}))
                    continue;
                grow(*next, v2(q));
                queue.push_back(*next);
            }
        }
    }
    if (cavity.empty()) {
        // Nothing in the mesh changes (a spot point removed from the outline, say): the edit
        // changes the current sources only.
        if (next)
            plan.resolved = next->points;
        return plan;
    }
    // ---- The adapter's input ----
    auto position = [&](Vec3 p) {
        if (const auto found = lifted.find(keyOf(p)); found != lifted.end())
            p.z = after.at.at(found->first).z;
        return p;
    };
    TerrainCavityInput input;
    std::map<TerrainPlanKey, std::uint32_t> index;
    std::vector<Vec3> positions;
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    for (const auto &ref : cavity) {
        const auto c = view.corners(ref);
        std::array<std::uint32_t, 3> tri{};
        for (int i = 0; i < 3; ++i) {
            const auto key = keyOf(c[std::size_t(i)]);
            if (removed.contains(key)) {
                tri[std::size_t(i)] = std::numeric_limits<std::uint32_t>::max();
                continue;
            }
            const auto p = position(c[std::size_t(i)]);
            const auto [it, fresh] = index.try_emplace(key, std::uint32_t(positions.size()));
            if (fresh) {
                positions.push_back(p);
                input.vertices.push_back({{p.x, p.y, p.z}, now.contour(key), sourceId});
            }
            tri[std::size_t(i)] = it->second;
        }
        for (int i = 0; i < 3; ++i)
            directed[{tri[std::size_t(i)], tri[std::size_t((i + 1) % 3)]}] += 1;
    }
    constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    for (const auto &[edge, uses] : directed) {
        const auto [a, b] = edge;
        if (directed.contains({b, a})) {
            if (a < b && a != none && b != none &&
                now.constraint(keyOf(positions[a]), keyOf(positions[b])))
                input.constraints.push_back({a, b});
            continue;
        }
        if (a == none || b == none) {
            problems.add("triangulation-failed", "A removed vertex lies on the cavity boundary.",
                         {}, sourceId);
            problems.refuse("Contour edit");
        }
        input.boundary.push_back({a, b});
    }
    // New segments as two-point polylines; inserted vertices on no new segment as points.
    std::vector<TerrainPlanKey> order; // the new input vertices, in adapter order
    std::set<TerrainPlanKey> onSegment;
    const bool contour = next && next->role == TerrainSourceRole::Contour;
    for (const auto &[a, b] : addedSegments) {
        if (contour)
            input.contours.push_back({a.z, {v2(a), v2(b)}, false, sourceId});
        else
            input.breaklines.push_back(
                {{{a.x, a.y, a.z}, {b.x, b.y, b.z}}, false, sourceId});
        onSegment.insert(keyOf(a));
        onSegment.insert(keyOf(b));
    }
    // Adapter order: contours' points, then breaklines', then points.
    for (const auto &[a, b] : addedSegments) {
        order.push_back(keyOf(a));
        order.push_back(keyOf(b));
    }
    for (const auto &[key, q] : inserted)
        if (!onSegment.contains(key)) {
            input.points.push_back({{q.x, q.y, q.z}, sourceId});
            order.push_back(key);
        }
    const auto result = retriangulateTerrainCavity(input, adapterOptions(context.parameters));
    if (!result.ok) {
        for (const auto &problem : result.report.refusals)
            problems.merge(problem);
        problems.refuse("Contour edit");
    }
    auto vertex = [&](std::uint32_t i) {
        if (i < positions.size())
            return positions[i];
        const auto &v = result.vertices[i];
        return Vec3{v.x, v.y, v.z};
    };
    for (const auto &t : result.triangles) {
        const Corners c{vertex(t[0]), vertex(t[1]), vertex(t[2])};
        if (length(cross(c[1] - c[0], c[2] - c[0])) < 1e-12)
            problems.add("degenerate-triangle",
                         "Triangles below the modelling tolerance (area under 5e-13 m²) "
                         "cannot become faces.",
                         {(c[0].x + c[1].x + c[2].x) / 3, (c[0].y + c[1].y + c[2].y) / 3},
                         sourceId);
        plan.change.added.push_back(c);
    }
    problems.check("Contour edit");
    plan.change.removed.assign(cavity.begin(), cavity.end());
    // The resolved geometry: each new vertex where the adapter placed it.
    std::map<TerrainPlanKey, Vec3> placed;
    for (std::size_t i = 0; i < order.size() && i < result.placed.size(); ++i)
        placed.try_emplace(order[i], vertex(result.placed[i]));
    if (next)
        for (const auto &p : next->points) {
            const auto found = placed.find(keyOf(p));
            plan.resolved.push_back(found != placed.end() ? found->second : p);
        }
    plan.cavityTriangles = cavity.size();
    plan.addedTriangles = plan.change.added.size();
    plan.liftedVertices = lifted.size();
    plan.removedVertices = removed.size();
    plan.insertedVertices = inserted.size();
    plan.mergedDuplicates = result.report.mergedDuplicates;
    plan.flips = result.report.flipsApplied;
    plan.flatRemaining = result.report.flatTrianglesRemaining;
    return plan;
}

// ---- Data -----------------------------------------------------------------------------------
bool TerrainSourceEditData::equals(const TerrainEditData &other) const {
    const auto *o = dynamic_cast<const TerrainSourceEditData *>(&other);
    return o && o->source == source && o->role == role && o->points == points &&
           o->before == before;
}
std::size_t TerrainSourceEditData::bytes() const {
    return sizeof(TerrainSourceEditData) + points.size() * sizeof(Vec3);
}
void validateTerrainSourceEditData(const std::string &kind, const TerrainSourceEditData &data) {
    auto fail = [](const std::string &message) { throw std::invalid_argument(message); };
    if (!data.source)
        fail("A source edit names a source id");
    const bool hasPoints = kind == terrainEditSourceKind || kind == terrainAddSourceKind;
    const bool hasBefore = kind == terrainEditSourceKind || kind == terrainRemoveSourceKind;
    if (!hasPoints && !hasBefore)
        fail("Unknown source edit kind");
    if (hasPoints) {
        if (data.role != TerrainSourceRole::Contour && data.role != TerrainSourceRole::Breakline &&
            data.role != TerrainSourceRole::Points)
            fail("A source edit's role is contour, breakline or points");
        const std::size_t minimum = data.role == TerrainSourceRole::Points ? 1 : 2;
        if (data.points.size() < minimum || data.points.size() > terrainSourceEditPoints)
            fail("A source edit has 1 (points) or 2 to 20,000 points");
        for (const auto &p : data.points)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                std::abs(p.x) > localBound || std::abs(p.y) > localBound ||
                std::abs(p.z) > localBound)
                fail("Source edit points are finite and within 1,000,000 m of the terrain");
        if (data.role == TerrainSourceRole::Contour)
            for (const auto &p : data.points)
                if (p.z != data.points.front().z)
                    fail("A contour has one elevation");
        if (data.role != TerrainSourceRole::Points && data.points.size() < 4 &&
            data.points.size() >= 2 && data.points.front() == data.points.back())
            fail("A closed polyline has at least three distinct points");
    }
    if (hasBefore) {
        const auto &b = data.before;
        for (const double v : b)
            if (!std::isfinite(v) || std::abs(v) > localBound)
                fail("A source edit's box is finite");
        if (b[0] > b[2] || b[1] > b[3])
            fail("A source edit's box has its minimum first");
    }
}
std::uint32_t terrainAddedSourceId(std::uint32_t maxBaseId, std::uint32_t edit) {
    const std::uint64_t id = std::uint64_t(maxBaseId) + edit;
    if (!edit || id > std::numeric_limits<std::uint32_t>::max())
        throw TerrainError("LIMIT_EXCEEDED", "The terrain has used every source id; use Reset "
                                             "edits, or Explode terrain to keep the surface as "
                                             "ordinary geometry");
    return std::uint32_t(id);
}

// ---- Promotion ------------------------------------------------------------------------------
std::vector<Vec3> terrainContourAtLevel(const TerrainEditMesh &mesh, double level,
                                        std::optional<std::array<double, 2>> near) {
    if (!std::isfinite(level))
        throw TerrainError("INVALID_REQUEST", "The contour level is a finite elevation");
    // Crossing points per mesh edge (canonical endpoint order), and one segment per crossed
    // triangle, oriented with higher ground on its left.
    struct EdgeKey {
        TerrainPositionKey a, b;
        bool operator==(const EdgeKey &) const = default;
    };
    struct EdgeHash {
        std::size_t operator()(const EdgeKey &k) const {
            const TerrainPositionHash h;
            return h(k.a) * 1000003u ^ h(k.b);
        }
    };
    std::unordered_map<EdgeKey, std::uint32_t, EdgeHash> edges;
    std::vector<Vec3> points;
    std::vector<std::uint32_t> edgeUses; // crossed triangles per crossing (1 on the outline)
    std::vector<char> atVertex;
    auto lexLess = [](const Vec3 &a, const Vec3 &b) {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    };
    auto crossing = [&](Vec3 p, Vec3 q) {
        if (lexLess(q, p))
            std::swap(p, q);
        const EdgeKey key{TerrainPositionKey::of(p), TerrainPositionKey::of(q)};
        const auto [it, fresh] = edges.try_emplace(key, std::uint32_t(points.size()));
        if (fresh) {
            const double t = (level - p.z) / (q.z - p.z);
            Vec3 at = t <= 0 ? p : t >= 1 ? q
                                          : Vec3{p.x + t * (q.x - p.x), p.y + t * (q.y - p.y), 0};
            atVertex.push_back(t <= 0 || t >= 1);
            at.z = level;
            points.push_back(at);
            edgeUses.push_back(0);
        }
        ++edgeUses[it->second];
        return it->second;
    };
    std::vector<std::array<std::uint32_t, 2>> segments;
    for (const auto *tile : mesh.tiles()) {
        const auto &content = *tile->content;
        if (tile->maxZ < level || tile->minZ >= level)
            continue;
        for (const auto &t : content.triangles) {
            const Corners c{content.vertices[t[0]], content.vertices[t[1]],
                            content.vertices[t[2]]};
            bool above[3];
            for (int i = 0; i < 3; ++i)
                above[i] = c[std::size_t(i)].z >= level;
            if (above[0] == above[1] && above[1] == above[2])
                continue;
            // The edge leaving the above region (counter-clockwise), then the one entering it: the
            // segment runs with the above region on its left.
            std::optional<std::uint32_t> from, to;
            for (int i = 0; i < 3; ++i) {
                const int j = (i + 1) % 3;
                if (above[i] && !above[j])
                    to = crossing(c[std::size_t(i)], c[std::size_t(j)]);
                else if (!above[i] && above[j])
                    from = crossing(c[std::size_t(i)], c[std::size_t(j)]);
            }
            segments.push_back({*from, *to});
        }
    }
    if (segments.empty())
        throw TerrainError("NOT_FOUND", "The terrain has no contour line at that elevation");
    // Chain: each crossing has at most one outgoing and one incoming segment.
    std::vector<std::uint32_t> next(points.size(), std::numeric_limits<std::uint32_t>::max()),
        previous(points.size(), std::numeric_limits<std::uint32_t>::max());
    for (const auto &[a, b] : segments) {
        next[a] = b;
        previous[b] = a;
    }
    constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    std::vector<char> used(points.size(), 0);
    struct Line {
        std::vector<std::uint32_t> ids;
        bool closed{};
    };
    std::vector<Line> lines;
    auto walk = [&](std::uint32_t start) {
        Line line;
        for (auto at = start; at != none && !used[at]; at = next[at]) {
            used[at] = 1;
            line.ids.push_back(at);
        }
        line.closed = line.ids.size() > 2 && next[line.ids.back()] == line.ids.front();
        lines.push_back(std::move(line));
    };
    // Open lines from their starts (in crossing order), then the loops.
    for (std::uint32_t i = 0; i < points.size(); ++i)
        if (!used[i] && previous[i] == none && next[i] != none)
            walk(i);
    for (std::uint32_t i = 0; i < points.size(); ++i)
        if (!used[i] && next[i] != none)
            walk(i);
    std::size_t chosen = 0;
    if (lines.size() > 1) {
        if (!near)
            throw TerrainError("INVALID_REQUEST",
                               std::to_string(lines.size()) +
                                   " contour lines lie at that elevation; give `at` to choose "
                                   "one");
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t l = 0; l < lines.size(); ++l) {
            const auto &ids = lines[l].ids;
            const std::size_t count = lines[l].closed ? ids.size() : ids.size() - 1;
            for (std::size_t i = 0; i < std::max<std::size_t>(count, 1); ++i) {
                const auto a = points[ids[i]], b = points[ids[(i + 1) % ids.size()]];
                const double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
                double t = len > 0 ? (((*near)[0] - a.x) * dx + ((*near)[1] - a.y) * dy) / len : 0;
                t = std::clamp(t, 0.0, 1.0);
                const double d = std::hypot(a.x + t * dx - (*near)[0], a.y + t * dy - (*near)[1]);
                if (d < best) {
                    best = d;
                    chosen = l;
                }
            }
        }
    }
    auto line = lines[chosen];
    if (!line.closed) {
        // Ends on the outline are dropped unless they are mesh vertices.
        auto outlineEnd = [&](std::uint32_t id) { return edgeUses[id] == 1 && !atVertex[id]; };
        while (!line.ids.empty() && outlineEnd(line.ids.front()))
            line.ids.erase(line.ids.begin());
        while (!line.ids.empty() && outlineEnd(line.ids.back()))
            line.ids.pop_back();
    }
    std::vector<TerrainVec3> polyline;
    for (const auto id : line.ids)
        polyline.push_back({points[id].x, points[id].y, points[id].z});
    TerrainPolylineCleaning counts;
    cleanTerrainPolyline(polyline, line.closed, counts);
    const bool closed = line.closed && polyline.size() >= 3;
    if (polyline.size() < 2)
        throw TerrainError("NOT_FOUND",
                           "The contour line at that elevation has no length inside the outline");
    std::vector<Vec3> result;
    for (const auto &p : polyline)
        result.push_back({p.x, p.y, level});
    if (closed)
        result.push_back(result.front());
    if (result.size() > terrainSourceEditPoints)
        throw TerrainError("LIMIT_EXCEEDED", "The contour line has more than 20,000 points");
    return result;
}

// ---- Kinds ----------------------------------------------------------------------------------
namespace {
const TerrainSourceEditData &dataOf(const TerrainEdit &edit) {
    return dynamic_cast<const TerrainSourceEditData &>(*edit.data);
}
std::array<double, 4> boxOf(const std::vector<Vec3> &points) {
    std::array<double, 4> box{std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()};
    for (const auto &p : points) {
        box[0] = std::min(box[0], p.x);
        box[1] = std::min(box[1], p.y);
        box[2] = std::max(box[2], p.x);
        box[3] = std::max(box[3], p.y);
    }
    return box;
}
std::array<double, 4> unite(std::array<double, 4> a, const std::array<double, 4> &b) {
    a[0] = std::min(a[0], b[0]);
    a[1] = std::min(a[1], b[1]);
    a[2] = std::max(a[2], b[2]);
    a[3] = std::max(a[3], b[3]);
    return a;
}
TerrainSource sourceOf(const TerrainEdit &edit, const TerrainSource *old) {
    const auto &data = dataOf(edit);
    TerrainSource source;
    source.id = data.source;
    source.role = data.role;
    source.points = data.points;
    source.reference = old ? old->reference : "edit " + std::to_string(edit.id);
    return source;
}
bool editable(const TerrainSource &source) {
    return source.role == TerrainSourceRole::Contour ||
           source.role == TerrainSourceRole::Breakline || source.role == TerrainSourceRole::Points;
}
TerrainEditOutcome skip(std::string notice) { return {std::nullopt, std::move(notice)}; }
TerrainEditOutcome applySource(const TerrainEditContext &context, const TerrainEdit &edit) {
    const auto &data = dataOf(edit);
    const auto *old = context.sources.find(data.source);
    const auto id = std::to_string(data.source);
    if (edit.kind == terrainAddSourceKind) {
        if (old)
            return skip("source " + id + " already exists");
        const auto next = sourceOf(edit, nullptr);
        return {planTerrainSourceEdit(context, nullptr, &next).change, {}};
    }
    if (!old)
        return skip("source " + id + " no longer exists");
    if (!editable(*old))
        return skip("source " + id + " is a " + roleText(old->role) +
                    ", which source edits do not change");
    if (edit.kind == terrainRemoveSourceKind)
        return {planTerrainSourceEdit(context, old, nullptr).change, {}};
    const auto next = sourceOf(edit, old);
    return {planTerrainSourceEdit(context, old, &next).change, {}};
}
void applySourceEffect(TerrainSourceState &state, const TerrainEdit &edit) {
    const auto &data = dataOf(edit);
    const auto *old = state.find(data.source);
    if (edit.kind == terrainAddSourceKind) {
        state.add(sourceOf(edit, nullptr));
        return;
    }
    if (!old || !editable(*old))
        return;
    if (edit.kind == terrainRemoveSourceKind)
        state.remove(data.source);
    else
        state.replace(sourceOf(edit, old));
}
} // namespace
std::vector<TerrainEditKind> terrainSourceEditKinds() {
    std::vector<TerrainEditKind> kinds;
    for (const char *name : {terrainEditSourceKind, terrainAddSourceKind, terrainRemoveSourceKind}) {
        TerrainEditKind kind;
        kind.name = name;
        kind.applyInContext = applySource;
        kind.sources = applySourceEffect;
        kind.bounds = [](const TerrainEdit &edit) {
            const auto &data = dataOf(edit);
            if (edit.kind == terrainAddSourceKind)
                return boxOf(data.points);
            if (edit.kind == terrainRemoveSourceKind)
                return data.before;
            return unite(boxOf(data.points), data.before);
        };
        kinds.push_back(std::move(kind));
    }
    return kinds;
}
} // namespace sketchy
