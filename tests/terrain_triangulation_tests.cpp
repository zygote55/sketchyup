// R095.b (ADR 0168 section 3): the terrain triangulation adapter. Each case builds plain
// input in local metres, runs triangulateTerrain and checks the mesh, the report or the
// refusal with its locations.
//   terrain_triangulation_tests [--fuzz-count N]
#include "fuzz/terrain_cavity_harness.hpp"
#include "fuzz/terrain_triangulation_harness.hpp"
#include "geometry/terrain_triangulation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <tuple>

using namespace sketchy;
namespace {

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

using Mesh = TerrainTriangulationResult;

double area(const Mesh &m) {
    double total = 0;
    for (const auto &t : m.triangles) {
        const auto &a = m.vertices[t[0]];
        const auto &b = m.vertices[t[1]];
        const auto &c = m.vertices[t[2]];
        total += ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
    }
    return total;
}
std::optional<std::uint32_t> find(const Mesh &m, double x, double y) {
    for (std::uint32_t i = 0; i < m.vertices.size(); ++i)
        if (m.vertices[i].x == x && m.vertices[i].y == y)
            return i;
    return std::nullopt;
}
bool hasEdge(const Mesh &m, std::uint32_t a, std::uint32_t b) {
    for (const auto &t : m.triangles)
        for (int i = 0; i < 3; ++i)
            if ((t[i] == a && t[(i + 1) % 3] == b) || (t[i] == b && t[(i + 1) % 3] == a))
                return true;
    return false;
}
bool hasEdgeAt(const Mesh &m, TerrainVec2 p, TerrainVec2 q) {
    const auto a = find(m, p.x, p.y), b = find(m, q.x, q.y);
    return a && b && hasEdge(m, *a, *b);
}
const TerrainProblem *problem(const Mesh &m, TerrainProblemKind kind) {
    for (const auto &p : m.report.refusals)
        if (p.kind == kind)
            return &p;
    return nullptr;
}
void validate(const Mesh &m, const TerrainTriangulationOptions &options = {}) {
    fuzz::checkTerrainResult(m, options);
}
Mesh run(const TerrainTriangulationInput &in, const TerrainTriangulationOptions &options = {}) {
    auto result = triangulateTerrain(in, options);
    validate(result, options);
    return result;
}
// Pinned digest of the triangle list: a library or flag change that alters a fixture's
// triangulation fails here and must update the generator identity (ADR section 3).
std::uint64_t digest(const Mesh &m) {
    std::uint64_t h = 1469598103934665603ull;
    for (const auto &t : m.triangles)
        for (auto v : t)
            h = (h ^ v) * 1099511628211ull;
    return h ^ m.vertices.size();
}

void pinned(const Mesh &m, std::uint64_t expected, const char *name) {
    if (digest(m) != expected) {
        char text[160];
        std::snprintf(text, sizeof text,
                      "%s digest changed: 0x%016llxull (update the generator identity)", name,
                      (unsigned long long)digest(m));
        throw std::runtime_error(text);
    }
}

TerrainContour ring(double z, double radius, int n, std::uint64_t id, double cx = 0,
                    double cy = 0) {
    TerrainContour c;
    c.z = z;
    c.closed = true;
    c.sourceId = id;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * M_PI * i / n;
        c.points.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a)});
    }
    return c;
}
TerrainTriangulationInput hill() {
    TerrainTriangulationInput in;
    in.contours.push_back(ring(0, 10, 24, 1));
    in.contours.push_back(ring(1, 7, 20, 2));
    in.contours.push_back(ring(2, 4, 16, 3));
    in.contours.push_back(ring(3, 1.5, 8, 4));
    return in;
}
TerrainTriangulationInput grid(int nx, int ny, double step = 1) {
    TerrainTriangulationInput in;
    std::uint64_t id = 1;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            in.points.push_back({{i * step, j * step, 0.1 * i + 0.2 * j}, id++});
    return in;
}

class Random {
  public:
    explicit Random(std::uint64_t seed) : s_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (s_ += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    double unit() { return double(next() >> 11) / double(1ull << 53); }
    int pick(int n) { return int(next() % std::uint64_t(n)); }

  private:
    std::uint64_t s_;
};

void simpleContours() {
    const auto in = hill();
    const auto m = run(in);
    check(m.ok, "concentric contours triangulate");
    // Every contour vertex is in the mesh at its elevation.
    for (const auto &c : in.contours)
        for (const auto &p : c.points) {
            const auto v = find(m, p.x, p.y);
            check(v && m.vertices[*v].z == c.z, "a contour vertex keeps its elevation");
        }
    for (const auto &c : in.contours)
        for (std::size_t i = 0; i < c.points.size(); ++i) {
            const auto &p = c.points[i];
            const auto &q = c.points[(i + 1) % c.points.size()];
            check(hasEdgeAt(m, p, q), "a contour segment is a mesh edge");
        }
    // The mesh is the convex hull of the outer ring (a regular 24-gon of radius 10).
    check(std::abs(area(m) - 0.5 * 24 * 100 * std::sin(2 * M_PI / 24)) < 1e-9,
          "the mesh covers the hull");
    check(m.report.constraintEdges == 24 + 20 + 16 + 8, "one constraint edge per contour segment");
    check(m.report.rawVertexCount == 68 && m.report.uniqueVertexCount == 68, "vertex counts");
    // Only the summit ring encloses flat triangles that nothing can flip or fill.
    check(m.report.flatTrianglesRemaining == 6 && m.report.flatTrianglesInitial >= 6,
          "the summit stays flat");
    check(m.report.flatAreaRemaining > 0 && m.report.flatLocations.size() == 6,
          "flat area and locations are reported");
    const auto again = run(in);
    check(digest(m) == digest(again), "hill is stable");
    pinned(m, 0x12c1794ce3c761edull, "hill");
}

void breakline() {
    auto in = grid(7, 7);
    TerrainBreakline line;
    line.sourceId = 900;
    line.points = {{0.5, 0.5, 5}, {3.5, 2.5, 6}, {2.5, 5.25, 7}};
    in.breaklines.push_back(line);
    const auto m = run(in);
    check(m.ok, "breakline triangulates");
    for (std::size_t i = 0; i < line.points.size(); ++i) {
        const auto v = find(m, line.points[i].x, line.points[i].y);
        check(v && m.vertices[*v].z == line.points[i].z,
              "a breakline vertex keeps its own elevation");
        if (i)
            check(hasEdgeAt(m, {line.points[i - 1].x, line.points[i - 1].y},
                            {line.points[i].x, line.points[i].y}),
                  "a breakline segment is a mesh edge");
    }
    // Breakline vertices are not contour vertices, so a breakline never makes a flat triangle.
    check(m.report.flatTrianglesInitial == 0, "breakline vertices are not contour vertices");
    // A closed breakline adds its closing segment.
    TerrainBreakline closed;
    closed.closed = true;
    closed.points = {{1.5, 1.5, 3}, {4.5, 1.5, 3}, {4.5, 4, 4}};
    auto in2 = grid(7, 7);
    in2.breaklines.push_back(closed);
    const auto m2 = run(in2);
    check(hasEdgeAt(m2, {4.5, 4}, {1.5, 1.5}), "a closed breakline closes");
}

void boundaryAndHole() {
    auto in = grid(11, 11);
    in.boundary = TerrainPolygon{{{1, 1}, {9, 1}, {9, 9}, {1, 9}}, 700};
    in.holes.push_back(TerrainPolygon{{{4, 4}, {6, 4}, {6, 6}, {4, 6}}, 701});
    auto m = run(in);
    check(m.ok, "boundary and hole triangulate");
    check(std::abs(area(m) - 60) < 1e-12, "boundary area minus hole area");
    for (const auto &v : m.vertices) {
        check(v.x >= 1 && v.x <= 9 && v.y >= 1 && v.y <= 9, "no vertex outside the boundary");
        check(!(v.x > 4 && v.x < 6 && v.y > 4 && v.y < 6), "no vertex inside the hole");
    }
    check(m.report.droppedOutsideVertices == 121 - 81 + 1,
          "points outside the boundary and inside the hole are dropped");
    check(m.report.interpolatedPolygonVertices == 0,
          "polygon corners on grid points take their elevations");
    check(hasEdgeAt(m, {1, 1}, {9, 1}) || find(m, 5, 1), "the boundary is a constraint");
    check(m.report.droppedHoles == 0, "the hole is inside");

    // Polygon vertices that are not source vertices are interpolated from the sources: the
    // plane z = 0.1 x + 0.2 y is reproduced exactly, and a corner outside the sources' hull
    // takes the nearest source elevation.
    auto in2 = grid(11, 11);
    in2.boundary = TerrainPolygon{{{0.5, 0.5}, {9.5, 0.5}, {9.5, 9.5}, {0.5, 9.5}}, 710};
    in2.holes.push_back(
        TerrainPolygon{{{3.25, 3.25}, {4.75, 3.25}, {4.75, 4.75}, {3.25, 4.75}}, 711});
    m = run(in2);
    check(m.ok && m.report.interpolatedPolygonVertices == 8 &&
              m.report.extrapolatedPolygonVertices == 0,
          "polygon vertices are interpolated");
    for (const auto &v : m.vertices)
        check(std::abs(v.z - (0.1 * v.x + 0.2 * v.y)) < 1e-12,
              "interpolation reproduces the plane");
    check(std::abs(area(m) - (81 - 2.25)) < 1e-9, "area of the second boundary and hole");
    TerrainTriangulationInput in3;
    for (double x : {0.0, 4.0})
        for (double y : {0.0, 4.0})
            in3.points.push_back({{x, y, x + y}, 1});
    in3.boundary = TerrainPolygon{{{-1, -1}, {5, -1}, {5, 5}, {-1, 5}}, 2};
    m = run(in3);
    check(m.ok && m.report.extrapolatedPolygonVertices == 4,
          "corners outside the hull take the nearest elevation");
    check(find(m, -1, -1) && m.vertices[*find(m, -1, -1)].z == 0 &&
              m.vertices[*find(m, 5, 5)].z == 8,
          "nearest source elevation");

    // A hole outside the boundary is dropped and counted; a hole around it is refused.
    auto in4 = grid(11, 11);
    in4.boundary = TerrainPolygon{{{1, 1}, {4, 1}, {4, 4}, {1, 4}}, 1};
    in4.holes.push_back(TerrainPolygon{{{6, 6}, {8, 6}, {8, 8}, {6, 8}}, 2});
    m = run(in4);
    check(m.ok && m.report.droppedHoles == 1 && std::abs(area(m) - 9) < 1e-12,
          "outside hole dropped");
    in4.holes.push_back(TerrainPolygon{{{0, 0}, {5, 0}, {5, 5}, {0, 5}}, 3});
    m = run(in4);
    check(!m.ok && problem(m, TerrainProblemKind::InvalidPolygon),
          "a hole around the boundary is refused");
    // Nested holes and degenerate polygons are refused.
    auto in5 = grid(11, 11);
    in5.holes.push_back(TerrainPolygon{{{2, 2}, {8, 2}, {8, 8}, {2, 8}}, 1});
    in5.holes.push_back(TerrainPolygon{{{4, 4}, {6, 4}, {6, 6}, {4, 6}}, 2});
    m = run(in5);
    check(!m.ok && problem(m, TerrainProblemKind::InvalidPolygon), "nested holes are refused");
    auto in6 = grid(5, 5);
    in6.boundary = TerrainPolygon{{{0, 0}, {4, 0}, {2, 0}}, 1};
    m = run(in6);
    check(!m.ok && problem(m, TerrainProblemKind::InvalidPolygon),
          "a zero-area boundary is refused");
    // Holes without a boundary: the hull is the outside.
    auto in7 = grid(11, 11);
    in7.holes.push_back(TerrainPolygon{{{4, 4}, {6, 4}, {6, 6}, {4, 6}}, 1});
    m = run(in7);
    check(m.ok && std::abs(area(m) - 96) < 1e-12, "a hole without a boundary");
}

void constraintsOnBoundary() {
    // A source vertex exactly on a boundary edge splits that edge; it is not an error.
    TerrainTriangulationInput in;
    in.boundary = TerrainPolygon{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 1};
    TerrainContour c;
    c.z = 5;
    c.sourceId = 2;
    c.points = {{5, 0}, {5, 5}};
    in.contours.push_back(c);
    for (double x : {0.0, 10.0})
        for (double y : {0.0, 10.0})
            in.points.push_back({{x, y, 1}, 3});
    auto m = run(in);
    check(m.ok && std::abs(area(m) - 100) < 1e-12, "a vertex on a boundary edge");
    check(hasEdgeAt(m, {0, 0}, {5, 0}) && hasEdgeAt(m, {5, 0}, {10, 0}),
          "the boundary edge is split at the vertex");
    // A contour with an end outside the boundary loses that segment, not the import.
    TerrainTriangulationInput clip;
    clip.boundary = TerrainPolygon{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 1};
    TerrainContour through;
    through.z = 2;
    through.sourceId = 2;
    through.points = {{-5, 5}, {5, 5}, {8, 8}};
    clip.contours.push_back(through);
    for (double x : {0.0, 10.0})
        for (double y : {0.0, 10.0})
            clip.points.push_back({{x, y, 1}, 3});
    m = run(clip);
    check(m.ok && m.report.droppedOutsideVertices == 1 && m.report.droppedOutsideSegments == 1,
          "a contour vertex outside the boundary is dropped with its segment");
    check(hasEdgeAt(m, {5, 5}, {8, 8}), "the rest of the contour stays");
    // Both ends inside but the segment leaves a concave boundary: refused, located.
    TerrainTriangulationInput concave;
    concave.boundary = TerrainPolygon{{{0, 0}, {10, 0}, {10, 4}, {4, 4}, {4, 10}, {0, 10}}, 1};
    TerrainContour cut;
    cut.z = 2;
    cut.sourceId = 9;
    cut.points = {{2, 8}, {8, 2}};
    concave.contours.push_back(cut);
    concave.points.push_back({{1, 1, 0}, 3});
    m = run(concave);
    const auto *p = problem(m, TerrainProblemKind::CrossingConstraints);
    check(!m.ok && p && p->count == 2 && p->locations.size() == 2,
          "a contour crossing the boundary twice");
    check(std::abs(p->locations[0].xy.x - 6) < 1e-9 && std::abs(p->locations[0].xy.y - 4) < 1e-9,
          "first crossing");
    check(std::abs(p->locations[1].xy.x - 4) < 1e-9 && std::abs(p->locations[1].xy.y - 6) < 1e-9,
          "second crossing");
}

void crossingRefused() {
    TerrainTriangulationInput in;
    TerrainContour a, b;
    a.z = 1;
    a.sourceId = 11;
    a.points = {{0, 0}, {4, 4}};
    b.z = 2;
    b.sourceId = 22;
    b.points = {{0, 4}, {4, 0}};
    in.contours = {a, b};
    in.points.push_back({{10, 10, 0}, 33});
    auto m = run(in);
    check(!m.ok && m.vertices.empty() && m.triangles.empty(), "crossing contours are refused");
    const auto *p = problem(m, TerrainProblemKind::CrossingConstraints);
    check(p && p->count == 1 && p->locations.size() == 1, "one crossing");
    check(std::abs(p->locations[0].xy.x - 2) < 1e-9 && std::abs(p->locations[0].xy.y - 2) < 1e-9,
          "at (2, 2)");
    check(p->locations[0].sourceA == 11 && p->locations[0].sourceB == 22,
          "with both source references");
    check(!p->message.empty() && p->message.size() <= terrainMaxMessageBytes, "a bounded message");

    // A contour that crosses itself.
    TerrainTriangulationInput bow;
    TerrainContour bowtie;
    bowtie.z = 1;
    bowtie.closed = true;
    bowtie.sourceId = 5;
    bowtie.points = {{0, 0}, {4, 4}, {4, 0}, {0, 4}};
    bow.contours.push_back(bowtie);
    m = run(bow);
    p = problem(m, TerrainProblemKind::CrossingConstraints);
    check(!m.ok && p && p->count == 1 && p->locations[0].sourceA == 5 &&
              p->locations[0].sourceB == 5,
          "a self-crossing contour");

    // Counts stay complete past the 32 locations, and every location is bounded.
    TerrainTriangulationInput comb;
    TerrainContour bar;
    bar.z = 0;
    bar.sourceId = 1;
    bar.points = {{-1, 0}, {100, 0}};
    comb.contours.push_back(bar);
    for (int i = 0; i < 40; ++i) {
        TerrainContour tooth;
        tooth.z = i + 1;
        tooth.sourceId = 100 + i;
        tooth.points = {{double(i) * 2 + 0.5, -1}, {double(i) * 2 + 0.5, 1}};
        comb.contours.push_back(tooth);
    }
    m = run(comb);
    p = problem(m, TerrainProblemKind::CrossingConstraints);
    check(!m.ok && p && p->count == 40 && p->locations.size() == terrainMaxLocationsPerKind,
          "40 crossings, 32 listed");

    // A vertex in the middle of a contour segment touches it; overlapping segments overlap.
    TerrainTriangulationInput touch;
    TerrainContour base;
    base.z = 0;
    base.sourceId = 1;
    base.points = {{0, 0}, {10, 0}};
    TerrainContour onto;
    onto.z = 0;
    onto.sourceId = 2;
    onto.points = {{5, 0}, {5, 5}};
    touch.contours = {base, onto};
    m = run(touch);
    p = problem(m, TerrainProblemKind::CrossingConstraints);
    check(!m.ok && p && p->count == 1 && p->locations[0].xy.x == 5 && p->locations[0].xy.y == 0,
          "a vertex inside a contour segment");
    TerrainTriangulationInput overlap;
    TerrainContour part;
    part.z = 0;
    part.sourceId = 3;
    part.points = {{3, 0}, {13, 0}};
    overlap.contours = {base, part};
    m = run(overlap);
    check(!m.ok && problem(m, TerrainProblemKind::CrossingConstraints),
          "overlapping collinear segments");

    // Contours that share a vertex at the same elevation, and repeated segments, are fine.
    TerrainTriangulationInput shared;
    TerrainContour s1, s2;
    s1.z = 3;
    s1.sourceId = 1;
    s1.points = {{0, 0}, {4, 0}, {4, 4}};
    s2.z = 3;
    s2.sourceId = 2;
    s2.points = {{4, 4}, {4, 0}, {8, 0}};
    shared.contours = {s1, s2};
    shared.points.push_back({{0, 6, 0}, 3});
    m = run(shared);
    check(m.ok && m.report.mergedDuplicates == 2 && m.report.mergedSegments == 1,
          "shared vertices and segments merge");
}

void conflictingDuplicates() {
    TerrainTriangulationInput in = grid(4, 4);
    TerrainContour c;
    c.z = 50;
    c.sourceId = 77;
    c.points = {{1, 1}, {1.5, 2.5}};
    in.contours.push_back(c);
    auto m = run(in);
    const auto *p = problem(m, TerrainProblemKind::ConflictingDuplicate);
    check(!m.ok && p && p->count == 1 && p->locations.size() == 1, "same x and y, different z");
    check(p->locations[0].xy.x == 1 && p->locations[0].xy.y == 1, "located at the duplicate");
    check(p->locations[0].sourceA == 77 && p->locations[0].sourceB == 6, "with both references");
    // Within the 1e-7 m tolerance counts as the same place.
    auto near = grid(4, 4);
    near.points.push_back({{1 + 5e-8, 1 - 5e-8, 100}, 99});
    m = run(near);
    check(!m.ok && problem(m, TerrainProblemKind::ConflictingDuplicate),
          "duplicates within tolerance conflict");
    // The same place at the same elevation merges and is counted; the first occurrence stays.
    auto same = grid(4, 4);
    same.points.push_back({{1 + 5e-8, 1, 0.1 + 0.2}, 99});
    m = run(same);
    check(m.ok && m.report.mergedDuplicates == 1 && m.report.uniqueVertexCount == 16,
          "merged duplicate");
    check(find(m, 1, 1) && m.vertices[*find(m, 1, 1)].z == 0.1 * 1 + 0.2 * 1,
          "the first occurrence is kept");
    // More than 32 conflicts: complete count, bounded locations.
    TerrainTriangulationInput many;
    for (int i = 0; i < 50; ++i) {
        many.points.push_back({{double(i), 0, 0}, std::uint64_t(i)});
        many.points.push_back({{double(i), 0, 1}, std::uint64_t(100 + i)});
    }
    m = run(many);
    p = problem(m, TerrainProblemKind::ConflictingDuplicate);
    check(!m.ok && p && p->count == 50 && p->locations.size() == 32, "50 conflicts, 32 listed");
}

void insufficientPoints() {
    TerrainTriangulationInput in;
    auto m = run(in);
    check(!m.ok && problem(m, TerrainProblemKind::InsufficientPoints), "no points");
    for (int n : {1, 2, 3, 10}) {
        TerrainTriangulationInput line;
        for (int i = 0; i < n; ++i)
            line.points.push_back({{double(i), 2.0 * i + 1, double(i)}, std::uint64_t(i + 1)});
        m = run(line);
        const auto *p = problem(m, TerrainProblemKind::InsufficientPoints);
        check(!m.ok && p && p->count == std::uint64_t(n) &&
                  p->locations.size() == std::min<std::size_t>(n, 32),
              "collinear or too few points are refused");
    }
    // Collinear contour vertices count the same way, and a duplicate does not add a third point.
    TerrainTriangulationInput dup;
    dup.points = {{{0, 0, 0}, 1}, {{1, 1, 0}, 2}, {{0, 0, 0}, 3}};
    m = run(dup);
    check(!m.ok && problem(m, TerrainProblemKind::InsufficientPoints), "two distinct points");
    // Everything dropped by the boundary leaves too few.
    TerrainTriangulationInput away = grid(3, 3);
    away.boundary = TerrainPolygon{{{10, 10}, {12, 10}, {12, 12}}, 1};
    m = run(away);
    check(!m.ok && problem(m, TerrainProblemKind::InsufficientPoints),
          "all sources outside the boundary");
}

void inputValidation() {
    auto in = grid(3, 3);
    in.points[4].position.z = std::nan("");
    auto m = run(in);
    const auto *p = problem(m, TerrainProblemKind::NonFinite);
    check(!m.ok && p && p->count == 1 && p->locations[0].sourceA == 5,
          "a non-finite elevation is refused");
    in = grid(3, 3);
    in.contours.push_back(ring(0, 1, 6, 9));
    in.contours.back().points[2].x = INFINITY;
    m = run(in);
    check(!m.ok && problem(m, TerrainProblemKind::NonFinite), "an infinite x is refused");
    // Source coordinates are refused: the adapter wants the local frame.
    TerrainTriangulationInput utm;
    utm.points = {{{512345.678, 5234567.891, 1850}, 1},
                  {{512350.678, 5234567.891, 1851}, 2},
                  {{512345.678, 5234572.891, 1852}, 3}};
    m = run(utm);
    p = problem(m, TerrainProblemKind::ExtentExceeded);
    check(!m.ok && p && p->count == 3 && p->message.find("local") != std::string::npos,
          "UTM coordinates are refused");
    // Limits.
    TerrainTriangulationOptions tight;
    tight.limits.maxRawVertices = 5;
    m = run(grid(3, 3), tight);
    p = problem(m, TerrainProblemKind::LimitExceeded);
    check(!m.ok && p && p->message.find("9 input vertices") != std::string::npos,
          "raw vertex limit states the count");
    tight = {};
    tight.limits.maxVertices = 8;
    m = run(grid(3, 3), tight);
    check(!m.ok && problem(m, TerrainProblemKind::LimitExceeded), "merged vertex limit");
    tight = {};
    tight.limits.maxPolygonVertices = 3;
    auto poly = grid(3, 3);
    poly.boundary = TerrainPolygon{{{0, 0}, {2, 0}, {2, 2}, {0, 2}}, 1};
    m = run(poly, tight);
    check(!m.ok && problem(m, TerrainProblemKind::LimitExceeded), "polygon vertex limit");
    // Zero-length segments and a repeated final vertex are dropped, not refused.
    TerrainTriangulationInput zero = grid(3, 3);
    TerrainContour c;
    c.z = 7;
    c.sourceId = 4;
    c.points = {{0.5, 0.5}, {0.5, 0.5}, {1.5, 0.5}, {1.5, 0.5}, {1.5, 1.5}};
    zero.contours.push_back(c);
    m = run(zero);
    check(m.ok && m.report.droppedZeroLengthSegments == 2 && m.report.mergedDuplicates == 2,
          "zero-length segments drop");
    // A lone vertex per contour is just a point.
    TerrainContour one;
    one.z = 9;
    one.points = {{0.25, 0.25}};
    zero.contours.push_back(one);
    m = run(zero);
    check(m.ok && find(m, 0.25, 0.25), "a one-vertex contour");
}

// The tent: a contour a-c-b whose free edge a-b is Delaunay, and a lower point d beyond it.
TerrainTriangulationInput tent() {
    TerrainTriangulationInput in;
    TerrainContour c;
    c.z = 0;
    c.sourceId = 1;
    c.points = {{0, 0}, {2, 3}, {4, 0}};
    in.contours.push_back(c);
    in.points.push_back({{2, -3, 1}, 2});
    return in;
}

void flatFlip() {
    TerrainTriangulationOptions off;
    off.flatMode = TerrainFlatMode::ReportOnly;
    auto m = run(tent(), off);
    check(m.ok && m.triangles.size() == 2, "tent, report only");
    check(m.report.flatTrianglesInitial == 1 && m.report.flatTrianglesRemaining == 1 &&
              m.report.flipsApplied == 0,
          "one flat triangle is reported");
    check(std::abs(m.report.flatAreaRemaining - 6) < 1e-12, "with its area");
    check(m.report.flatLocations.size() == 1 && std::abs(m.report.flatLocations[0].y - 1) < 1e-12,
          "and its centroid");
    check(hasEdgeAt(m, {0, 0}, {4, 0}), "the Delaunay edge joins the two same-elevation vertices");

    m = run(tent());
    check(m.ok && m.report.flipsApplied == 1 && m.report.flatTrianglesRemaining == 0,
          "the flip removes the flat");
    check(hasEdgeAt(m, {2, 3}, {2, -3}) && !hasEdgeAt(m, {0, 0}, {4, 0}),
          "across the quadrilateral");
    check(hasEdgeAt(m, {0, 0}, {2, 3}) && hasEdgeAt(m, {2, 3}, {4, 0}),
          "the contour segments remain edges");
    check(std::abs(area(m) - 12) < 1e-12, "the area is unchanged");

    // A flip that would break a constraint is never made: make a-b a contour segment.
    auto closed = tent();
    closed.contours[0].closed = true;
    m = run(closed);
    check(m.ok && m.report.flipsApplied == 0 && m.report.flatTrianglesRemaining == 1,
          "a constraint edge is not flipped");
    check(hasEdgeAt(m, {0, 0}, {4, 0}), "the constraint stays");

    // The summit ring of a hill is all flat and cannot be flipped away.
    m = run(hill());
    check(m.report.flatTrianglesRemaining == 6, "an enclosed flat region stays");
    // On a terrain built from rings, flips never add flats and never touch constraints.
    TerrainTriangulationOptions report = {};
    report.flatMode = TerrainFlatMode::ReportOnly;
    const auto before = run(hill(), report);
    check(m.report.flatTrianglesRemaining <= before.report.flatTrianglesRemaining,
          "flips only remove flats");
    check(m.report.flatTrianglesInitial == before.report.flatTrianglesInitial,
          "the initial count is the Delaunay count");
}

// The same flat triangle with a quadrilateral that is not convex: only Steiner fill can help.
TerrainTriangulationInput bentTent() {
    TerrainTriangulationInput in;
    TerrainContour c;
    c.z = 0;
    c.sourceId = 1;
    c.points = {{0, 0}, {6, 1}, {4, 0}};
    in.contours.push_back(c);
    in.points.push_back({{2, -3, 1}, 2});
    return in;
}

void steinerFill() {
    auto m = run(bentTent());
    check(m.ok && m.report.flatTrianglesInitial == 1 && m.report.flipsApplied == 0 &&
              m.report.flatTrianglesRemaining == 1,
          "off by default: the flat triangle is reported");
    check(m.triangles.size() == 3 && m.report.steinerPointsAdded == 0, "no points are invented");
    TerrainTriangulationOptions fill;
    fill.steinerFill = true;
    m = run(bentTent(), fill);
    check(m.ok && m.report.steinerPointsAdded == 1 && m.report.flatTrianglesRemaining == 0,
          "Steiner fill resolves it");
    check(m.vertices.size() == 5 && m.triangles.size() == 5, "one point, two more triangles");
    const auto mid = find(m, 2, 0);
    check(mid && m.vertices[*mid].z == 0.5,
          "the midpoint of the free edge, halfway to the neighbour's vertex");
    check(std::abs(area(m) - area(run(bentTent()))) < 1e-12, "area is unchanged by the fill");
    // The new point is not a contour vertex and the contour segments survive.
    check(hasEdgeAt(m, {0, 0}, {6, 1}) && hasEdgeAt(m, {6, 1}, {4, 0}),
          "constraint edges survive the fill");
    // Fill does not touch a region it cannot reach: the hill's summit stays reported.
    m = run(hill(), fill);
    check(m.ok && m.report.flatTrianglesRemaining == 6 &&
              m.report.steinerPointsAdded <= m.report.flatTrianglesInitial,
          "an enclosed summit is left and reported");
    // The fill is deterministic.
    check(digest(run(hill(), fill)) == digest(m), "fill is stable");
}

void maxEdge() {
    TerrainTriangulationInput in = grid(5, 5);
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 5; ++i)
            in.points.push_back({{20.0 + i, double(j), 1}, 100u + j * 5 + i});
    auto m = run(in);
    check(m.ok && m.report.trimmedTriangles == 0, "no limit, no trimming");
    TerrainTriangulationOptions limit;
    limit.maxEdgeLength = 3;
    m = run(in, limit);
    check(m.ok && m.report.trimmedTriangles > 0 && std::abs(area(m) - 32) < 1e-12,
          "long outer triangles are trimmed");
    for (const auto &t : m.triangles)
        for (int i = 0; i < 3; ++i) {
            const auto &a = m.vertices[t[i]];
            const auto &b = m.vertices[t[(i + 1) % 3]];
            check(std::hypot(a.x - b.x, a.y - b.y) <= 3, "no edge beyond the limit remains");
        }
    check(m.report.unusedVertices == 0, "every vertex is still used");
    // A limit that removes everything is refused.
    limit.maxEdgeLength = 0.5;
    m = run(grid(4, 4), limit);
    check(!m.ok && problem(m, TerrainProblemKind::InsufficientPoints), "nothing left");
}

void determinism() {
    // The same input in the same order gives identical bits, repeatedly.
    TerrainTriangulationOptions fill;
    fill.steinerFill = true;
    for (const auto &options : {TerrainTriangulationOptions{}, fill}) {
        const auto first = run(hill(), options);
        for (int i = 0; i < 5; ++i) {
            const auto again = run(hill(), options);
            check(fuzz::sameResult(first, again), "repeated runs are identical");
            check(std::memcmp(first.vertices.data(), again.vertices.data(),
                              first.vertices.size() * sizeof(TerrainVec3)) == 0,
                  "vertices are bit-identical");
        }
    }
    // Input order matters for numbering and for ties; for points in general position the
    // triangulation itself does not depend on it.
    Random random(42);
    TerrainTriangulationInput in;
    for (int i = 0; i < 400; ++i)
        in.points.push_back(
            {{random.unit() * 100, random.unit() * 100, random.unit() * 10}, std::uint64_t(i)});
    const auto base = run(in);
    using Tri3 = std::array<std::array<double, 3>, 3>;
    const auto geometric = [](const Mesh &m) {
        std::set<Tri3> out;
        for (const auto &t : m.triangles) {
            Tri3 g{};
            for (int i = 0; i < 3; ++i)
                g[i] = {m.vertices[t[i]].x, m.vertices[t[i]].y, m.vertices[t[i]].z};
            std::rotate(g.begin(), std::min_element(g.begin(), g.end()), g.end());
            out.insert(g);
        }
        return out;
    };
    auto shuffled = in;
    for (std::size_t i = shuffled.points.size() - 1; i > 0; --i)
        std::swap(shuffled.points[i], shuffled.points[random.pick(int(i + 1))]);
    const auto other = run(shuffled);
    check(other.ok && geometric(base) == geometric(other),
          "points in general position: the same triangles");
    check(base.vertices != other.vertices, "but numbered by input order");
}

void budget() {
    // Exactly the limit passes, one more is refused with the count.
    auto in = grid(8, 8);
    const auto full = run(in);
    check(full.ok, "8 x 8 grid");
    const auto count = full.triangles.size();
    TerrainTriangulationOptions limit;
    limit.limits.maxTriangles = count;
    check(run(in, limit).ok, "at the limit");
    limit.limits.maxTriangles = count - 1;
    auto m = run(in, limit);
    const auto *p = problem(m, TerrainProblemKind::TriangleBudget);
    check(!m.ok && p && p->message.find(std::to_string(count)) != std::string::npos &&
              p->message.find(std::to_string(count - 1)) != std::string::npos,
          "over the limit: the count and the limit are stated");
    // The real limit: 250,001 or more triangles refuse. A 360 x 360 grid has 257,762.
    m = run(grid(360, 360));
    p = problem(m, TerrainProblemKind::TriangleBudget);
    check(!m.ok && p && p->message.find("257762") != std::string::npos &&
              p->message.find("250000") != std::string::npos,
          "the 250,000 triangle budget refuses with the count");
    check(m.vertices.empty() && m.triangles.empty(), "and returns no mesh");
    // 353 x 353 = 2 x 352^2 = 247,808 triangles fit.
    m = run(grid(353, 353));
    check(m.ok && m.triangles.size() == 247808, "a terrain under the budget");
    // Steiner points count against the budget too.
    TerrainTriangulationOptions fill;
    fill.steinerFill = true;
    fill.limits.maxTriangles = 3;
    m = run(bentTent(), fill);
    check(!m.ok && problem(m, TerrainProblemKind::TriangleBudget),
          "Steiner fill is held to the budget");
}

void utmScale() {
    // Source coordinates at UTM scale, converted to a local frame by subtracting a site origin.
    const double originE = 512000, originN = 5234000, datum = 1850;
    struct Source {
        double e, n, z;
    };
    std::vector<Source> sources;
    Random random(7);
    for (int i = 0; i < 300; ++i)
        sources.push_back({512345.678 + std::round(random.unit() * 2000) / 1000 * 400 - 400,
                           5234567.891 + std::round(random.unit() * 2000) / 1000 * 400 - 400,
                           datum + std::round(random.unit() * 5000) / 1000});
    TerrainTriangulationInput local;
    for (std::size_t i = 0; i < sources.size(); ++i)
        local.points.push_back(
            {{sources[i].e - originE, sources[i].n - originN, sources[i].z - datum}, i + 1});
    // The conversion is exact: adding the origin back reproduces the source bit for bit.
    for (std::size_t i = 0; i < sources.size(); ++i) {
        check(local.points[i].position.x + originE == sources[i].e &&
                  local.points[i].position.y + originN == sources[i].n &&
                  local.points[i].position.z + datum == sources[i].z,
              "local coordinates convert back exactly");
    }
    const auto m = run(local);
    check(m.ok, "the local terrain triangulates");
    std::set<std::tuple<double, double, double>> expected;
    for (const auto &p : local.points)
        expected.insert({p.position.x, p.position.y, p.position.z});
    check(m.vertices.size() == expected.size(), "no vertex is added or lost");
    for (const auto &v : m.vertices) {
        check(expected.contains({v.x, v.y, v.z}),
              "every vertex keeps its local coordinates bit for bit");
    }
    // Within 1e-9 m of the source after adding the origin back, at the source's own precision.
    for (const auto &v : m.vertices) {
        bool found = false;
        for (const auto &s : sources)
            found = found ||
                    (std::abs(v.x + originE - s.e) <= 1e-9 &&
                     std::abs(v.y + originN - s.n) <= 1e-9 && std::abs(v.z + datum - s.z) <= 1e-9);
        check(found, "within 1e-9 m of a source vertex after the origin is added back");
    }
    // The triangulation is the same wherever the origin is chosen only up to the local
    // coordinates fed in; this fixture's digest is pinned.
    check(digest(m) == digest(run(local)), "stable");
    pinned(m, 0xb1d63c0e1a829aebull, "UTM-origin fixture");
}

void reportBounds() {
    // Large refusals keep every list and message bounded.
    TerrainTriangulationInput in;
    for (int i = 0; i < 100; ++i) {
        TerrainContour c;
        c.z = i;
        c.sourceId = 1000 + i;
        c.points = {{double(i), -5}, {double(i), 5}};
        in.contours.push_back(c);
    }
    TerrainContour across;
    across.z = 500;
    across.sourceId = 1;
    across.points = {{-1, 0}, {101, 0}};
    in.contours.push_back(across);
    for (int i = 0; i < 100; ++i)
        in.points.push_back({{double(i) + 0.5, 3, 0}, 0});
    in.points.push_back({{0, -5, 99}, 7});
    const auto m = run(in);
    check(!m.ok, "a heavy refusal");
    for (const auto &p : m.report.refusals) {
        check(p.locations.size() <= 32 && p.message.size() <= 256, "bounded");
        check(p.count >= p.locations.size(), "counts are complete");
    }
    check(problem(m, TerrainProblemKind::CrossingConstraints)->count == 100,
          "100 crossings counted");
    check(problem(m, TerrainProblemKind::ConflictingDuplicate)->count == 1, "and the duplicate");
}

// Random points with a rectangular boundary and a rectangular hole whose corners are off the
// point grid: the mesh is exactly the boundary minus the hole, whatever the points are.
void boundaryAreaProperty() {
    Random random(99);
    int accepted = 0;
    for (int round = 0; round < 300; ++round) {
        TerrainTriangulationInput in;
        for (int i = 0, n = 40 + random.pick(80); i < n; ++i) {
            const double x = double(random.pick(24)) - 2, y = double(random.pick(24)) - 2;
            in.points.push_back({{x, y, std::fmod(x + y + 4, 5)}, std::uint64_t(i + 1)});
        }
        const double x0 = 2 + random.pick(4) + 0.25, x1 = 12 + random.pick(6) + 0.5;
        const double y0 = 2 + random.pick(4) + 0.125, y1 = 12 + random.pick(6) + 0.75;
        in.boundary = TerrainPolygon{{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, 1};
        double expected = (x1 - x0) * (y1 - y0);
        if (random.pick(2)) {
            const double hx0 = x0 + 1.5, hx1 = hx0 + 1 + random.pick(3), hy0 = y0 + 1.5,
                         hy1 = hy0 + 1 + random.pick(3);
            in.holes.push_back(TerrainPolygon{{{hx0, hy0}, {hx1, hy0}, {hx1, hy1}, {hx0, hy1}}, 2});
            expected -= (hx1 - hx0) * (hy1 - hy0);
        }
        const auto m = run(in);
        if (!m.ok) {
            // Only too few usable points can refuse this input.
            check(problem(m, TerrainProblemKind::InsufficientPoints) != nullptr &&
                      m.report.refusals.size() == 1,
                  "only too few points refuse a clean boundary");
            continue;
        }
        ++accepted;
        check(std::abs(area(m) - expected) < 1e-9, "the mesh is the boundary minus the hole");
    }
    check(accepted > 250, "most rounds have enough points");
}

void generatedInputs(int count) {
    // Seeds: valid encodings of the fixtures above decode to the same input and run.
    const std::vector<fuzz::TerrainCase> seeds = [] {
        std::vector<fuzz::TerrainCase> out;
        out.push_back({hill(), {}});
        out.push_back({tent(), {}});
        fuzz::TerrainCase fill{bentTent(), {}};
        fill.options.steinerFill = true;
        out.push_back(fill);
        fuzz::TerrainCase bounded{grid(6, 6), {}};
        bounded.input.boundary = TerrainPolygon{{{1, 1}, {4, 1}, {4, 4}, {1, 4}}, 1};
        bounded.input.holes.push_back(TerrainPolygon{{{2, 2}, {3, 2}, {3, 3}, {2, 3}}, 2});
        bounded.options.maxEdgeLength = 3;
        out.push_back(bounded);
        return out;
    }();
    std::vector<std::vector<std::uint8_t>> corpus;
    for (const auto &seed : seeds) {
        const auto bytes = fuzz::encodeTerrainCase(seed);
        const auto decoded = fuzz::decodeTerrainCase(bytes.data(), bytes.size());
        check(decoded.input.contours.size() == seed.input.contours.size() &&
                  decoded.input.points.size() == seed.input.points.size() &&
                  decoded.input.holes.size() == seed.input.holes.size() &&
                  decoded.input.boundary.has_value() == seed.input.boundary.has_value(),
              "a seed decodes to its input");
        check(fuzz::runTerrainHarness(bytes.data(), bytes.size()), "a seed is accepted");
        corpus.push_back(bytes);
    }
    // Structured random input on a coarse grid, so duplicates, collinear runs, crossings and
    // co-circular ties are common, and byte-level mutations of the seeds.
    Random random(2026);
    int accepted = 0, refused = 0;
    for (int i = 0; i < count; ++i) {
        fuzz::TerrainCase c;
        const int span = 2 + random.pick(6);
        const auto coordinate = [&]() { return double(random.pick(span)) / 2; };
        std::uint64_t id = 1;
        for (int k = random.pick(3); k > 0; --k) {
            TerrainContour contour;
            contour.z = random.pick(3);
            contour.closed = random.pick(2);
            contour.sourceId = id++;
            for (int n = 1 + random.pick(6); n > 0; --n)
                contour.points.push_back({coordinate(), coordinate()});
            c.input.contours.push_back(contour);
        }
        for (int k = random.pick(2); k > 0; --k) {
            TerrainBreakline line;
            line.closed = random.pick(2);
            line.sourceId = id++;
            for (int n = 1 + random.pick(5); n > 0; --n)
                line.points.push_back({coordinate(), coordinate(), double(random.pick(4))});
            c.input.breaklines.push_back(line);
        }
        for (int n = random.pick(12); n > 0; --n)
            c.input.points.push_back({{coordinate(), coordinate(), double(random.pick(4))}, id++});
        if (random.pick(4) == 0) {
            TerrainPolygon p;
            p.sourceId = id++;
            for (int n = 3 + random.pick(3); n > 0; --n)
                p.points.push_back({coordinate(), coordinate()});
            c.input.boundary = p;
        }
        if (random.pick(5) == 0) {
            TerrainPolygon p;
            p.sourceId = id++;
            for (int n = 3 + random.pick(2); n > 0; --n)
                p.points.push_back({coordinate(), coordinate()});
            c.input.holes.push_back(p);
        }
        c.options.steinerFill = random.pick(2);
        c.options.flatMode =
            random.pick(5) == 0 ? TerrainFlatMode::ReportOnly : TerrainFlatMode::Flip;
        if (random.pick(4) == 0)
            c.options.maxEdgeLength = 0.5 + random.pick(4);
        auto bytes = fuzz::encodeTerrainCase(c);
        if (i % 3 == 2) {
            bytes = corpus[std::size_t(random.pick(int(corpus.size())))];
            for (int k = 1 + random.pick(4); k > 0; --k)
                bytes[std::size_t(random.pick(int(bytes.size())))] = std::uint8_t(random.next());
            if (random.pick(4) == 0)
                bytes.resize(std::size_t(random.pick(int(bytes.size()))));
        }
        (fuzz::runTerrainHarness(bytes.data(), bytes.size()) ? accepted : refused)++;
    }
    std::cout << "generated inputs: " << accepted << " accepted, " << refused << " refused\n";
    check(accepted > count / 20 && refused > count / 20, "the generator reaches both outcomes");
}

// D-044: progress and cancel. With a TerrainProgress a run gives the same mesh and report, its
// reports rise from 0 to 1, and a cancel at any report returns a cancelled result that keeps
// nothing. A cancel during CDT's vertex insertion stops inside it (the library's abort hook).
void progressAndCancel() {
    struct Case {
        const char *name;
        TerrainTriangulationInput input;
        TerrainTriangulationOptions options;
    };
    std::vector<Case> cases;
    cases.push_back({"hill", hill(), {}});
    {
        TerrainTriangulationOptions fill;
        fill.steinerFill = true;
        fill.maxEdgeLength = 6;
        cases.push_back({"hill, fill and trim", hill(), fill});
    }
    {
        auto in = grid(30, 30);
        // Corners off the grid: their elevations come from a first triangulation.
        in.boundary = TerrainPolygon{{{1.5, 1.5}, {27.5, 1.5}, {27.5, 27.5}, {1.5, 27.5}}, 700};
        in.holes.push_back(TerrainPolygon{{{10.5, 10.5}, {14.5, 10.5}, {14.5, 14.5}}, 701});
        cases.push_back({"grid with boundary and hole", in, {}});
    }
    for (const auto &c : cases) {
        const auto plain = triangulateTerrain(c.input, c.options);
        check(plain.ok, "the progress fixture triangulates");
        std::vector<std::pair<int, double>> reports;
        TerrainProgress progress({}, [&](TerrainStage stage, double fraction) {
            reports.emplace_back(int(stage), fraction);
        });
        const auto reported = triangulateTerrain(c.input, c.options, &progress);
        check(reported.ok && !reported.cancelled && reported.vertices == plain.vertices &&
                  reported.triangles == plain.triangles &&
                  reported.report.flatTrianglesRemaining == plain.report.flatTrianglesRemaining &&
                  reported.report.interpolatedPolygonVertices ==
                      plain.report.interpolatedPolygonVertices &&
                  reported.report.trimmedTriangles == plain.report.trimmedTriangles &&
                  reported.report.steinerPointsAdded == plain.report.steinerPointsAdded,
              "progress does not change the result");
        check(reports.size() >= 10, "a run reports its phases");
        check(reports.front() == std::pair<int, double>{int(TerrainStage::Triangulate), 0} &&
                  reports.back() == std::pair<int, double>{int(TerrainStage::Triangulate), 1},
              "progress runs from 0 to 1");
        for (std::size_t i = 1; i < reports.size(); ++i)
            check(reports[i].first == reports[0].first && reports[i].second > reports[i - 1].second,
                  "progress only rises");
        for (std::size_t n = 0; n < reports.size(); ++n) {
            bool cancel = false;
            std::size_t seen = 0;
            TerrainProgress cancelling([&] { return cancel; },
                                       [&](TerrainStage, double) { cancel = cancel || seen++ == n; });
            const auto cancelled = triangulateTerrain(c.input, c.options, &cancelling);
            check(cancelled.cancelled && !cancelled.ok && cancelled.vertices.empty() &&
                      cancelled.triangles.empty() && cancelled.report.rawVertexCount == 0 &&
                      cancelled.report.triangleCount == 0 && cancelled.report.refusals.empty() &&
                      cancelled.report.flatLocations.empty(),
                  "a cancel at any report keeps nothing");
            check(seen == n + 1, "the run stops at the checkpoint that saw the cancel");
        }
        // Cancelled before it starts.
        TerrainProgress before([] { return true; });
        check(triangulateTerrain(c.input, c.options, &before).cancelled,
              "a cancelled progress stops the run at once");
    }
    // Inside CDT: about 10,000 points; vertex insertion spans 0.35 to 0.665 of the stage.
    const auto in = grid(100, 100, 0.5);
    bool cancel = false;
    double at = -1;
    TerrainProgress inside([&] { return cancel; }, [&](TerrainStage, double fraction) {
        if (!cancel && fraction > .4 && fraction < .6) {
            cancel = true;
            at = fraction;
        }
    });
    const auto stopped = triangulateTerrain(in, {}, &inside);
    check(stopped.cancelled && stopped.triangles.empty() && at > .4 && at < .6,
          "a cancel during CDT vertex insertion stops the run there");
}

} // namespace

// ---- R096.c: the cavity API and the elevation-only flip (ADR 0169 §3, §4.2) ---------------
fuzz::CavityCase cavityGrid(int n, double cx, double cy, double radius) {
    fuzz::CavityCase c;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            c.base.push_back({double(i), double(j), 0.25 * i + 0.5 * j});
    c.cx = cx;
    c.cy = cy;
    c.radius = radius;
    return c;
}
bool refusedWith(const TerrainCavityResult &r, TerrainProblemKind kind) {
    if (r.ok)
        return false;
    for (const auto &problem : r.report.refusals)
        if (problem.kind == kind)
            return !problem.message.empty();
    return false;
}
void cavityBasics() {
    // A breakline and a spot point inserted into the middle of a grid; the boundary is kept.
    auto c = cavityGrid(7, 3, 3, 1.6);
    c.breaklines = {TerrainBreakline{{{2.5, 2.25, 2.5}, {3.5, 3.6, 3.5}}, false, 7}};
    c.points = {{{3.25, 2.75, 3}, 8}};
    const auto setup = fuzz::cavitySetup(c, false);
    check(setup.has_value(), "the grid has a cavity");
    const auto result = retriangulateTerrainCavity(setup->input);
    fuzz::checkCavityResult(*setup, result);
    check(result.ok && result.vertices.size() == setup->input.vertices.size() + 3,
          "the cavity takes the breakline and the point");
    check(result.placed.size() == 3 && result.placed[0] >= setup->input.vertices.size(),
          "new vertices are placed after the kept ones");
    // The same cavity with its vertices in another order gives the same triangles by position.
    const auto reversed = fuzz::cavitySetup(c, true);
    const auto again = retriangulateTerrainCavity(reversed->input);
    check(fuzz::positionTriangles(result) == fuzz::positionTriangles(again),
          "the cavity is canonical by position");
    // A new vertex within the merge tolerance of a kept one, at its elevation, merges into it.
    auto merge = cavityGrid(6, 2.5, 2.5, 1.2);
    merge.points = {{{2 + 5e-8, 2, 0.25 * 2 + 0.5 * 2}, 5}};
    const auto merged = fuzz::cavitySetup(merge, false);
    const auto mergedResult = retriangulateTerrainCavity(merged->input);
    check(mergedResult.ok && mergedResult.report.mergedDuplicates == 1 &&
              mergedResult.placed[0] < merged->input.vertices.size() &&
              mergedResult.vertices[mergedResult.placed[0]].x == 2,
          "a new vertex merges into a kept one and names it");
}
void cavityRefusals() {
    auto duplicate = cavityGrid(6, 2.5, 2.5, 1.2);
    duplicate.points = {{{2, 2, 9}, 5}};
    check(refusedWith(retriangulateTerrainCavity(fuzz::cavitySetup(duplicate, false)->input),
                      TerrainProblemKind::ConflictingDuplicate),
          "a new vertex on a kept one at another elevation is a conflicting duplicate");
    auto crossing = cavityGrid(8, 3.5, 3.5, 2.5);
    crossing.breaklines = {TerrainBreakline{{{2.5, 2.5, 1}, {4.5, 4.5, 1}}, false, 1},
                           TerrainBreakline{{{2.5, 4.5, 1}, {4.5, 2.5, 1}}, false, 2}};
    const auto crossed = retriangulateTerrainCavity(fuzz::cavitySetup(crossing, false)->input);
    check(refusedWith(crossed, TerrainProblemKind::CrossingConstraints) &&
              !crossed.report.refusals.front().locations.empty(),
          "crossing new segments are refused, located");
    // A new segment running into a kept constraint (a touch) is refused too.
    auto touching = cavityGrid(8, 3.5, 3.5, 2.5);
    touching.stride = 1;
    touching.points = {};
    const auto touchSetup = fuzz::cavitySetup(touching, false);
    auto touchInput = touchSetup->input;
    const auto &[ka, kb] = touchInput.constraints.front();
    const auto pa = touchInput.vertices[ka].position, pb = touchInput.vertices[kb].position;
    const TerrainVec3 mid{(pa.x + pb.x) / 2, (pa.y + pb.y) / 2, 7};
    touchInput.points = {{mid, 9}};
    check(refusedWith(retriangulateTerrainCavity(touchInput), TerrainProblemKind::CrossingConstraints),
          "a new vertex on a kept constraint is refused");
    auto outside = cavityGrid(6, 1, 1, 0.8);
    outside.points = {{{4.25, 4.25, 1}, 5}};
    check(refusedWith(retriangulateTerrainCavity(fuzz::cavitySetup(outside, false)->input),
                      TerrainProblemKind::InvalidPolygon),
          "a new vertex outside the cavity is refused");
    auto open = fuzz::cavitySetup(cavityGrid(6, 2.5, 2.5, 1.5), false)->input;
    open.boundary.pop_back();
    check(refusedWith(retriangulateTerrainCavity(open), TerrainProblemKind::InvalidPolygon),
          "an open boundary is refused");
    auto reversedBoundary = fuzz::cavitySetup(cavityGrid(6, 2.5, 2.5, 1.5), false)->input;
    for (auto &edge : reversedBoundary.boundary)
        std::swap(edge[0], edge[1]);
    check(refusedWith(retriangulateTerrainCavity(reversedBoundary),
                      TerrainProblemKind::InvalidPolygon),
          "a boundary with the cavity on its right is refused");
    auto nan = fuzz::cavitySetup(cavityGrid(6, 2.5, 2.5, 1.5), false)->input;
    nan.points = {{{std::nan(""), 1, 1}, 3}};
    check(refusedWith(retriangulateTerrainCavity(nan), TerrainProblemKind::NonFinite),
          "a non-finite new vertex is refused");
}
void cavityFlats() {
    // Flat triangles (three contour vertices at one elevation) are flipped inside the cavity only.
    auto c = cavityGrid(9, 4, 4, 2.6);
    for (auto &p : c.base)
        p.z = 0;
    c.flags = 2;
    c.contours = {TerrainContour{1, {{3.5, 3.5}, {4.5, 3.5}, {4.5, 4.5}, {3.5, 4.5}}, true, 9}};
    const auto setup = fuzz::cavitySetup(c, false);
    const auto flipped = retriangulateTerrainCavity(setup->input);
    fuzz::checkCavityResult(*setup, flipped);
    TerrainTriangulationOptions reportOnly;
    reportOnly.flatMode = TerrainFlatMode::ReportOnly;
    const auto kept = retriangulateTerrainCavity(setup->input, reportOnly);
    check(flipped.ok && kept.ok && kept.report.flipsApplied == 0 &&
              flipped.report.flatTrianglesRemaining <= kept.report.flatTrianglesRemaining,
          "the flat flip runs inside the cavity, not in report-only mode");
    // The elevation-only flip: a flat triangle beside a lower vertex is flipped away, unless
    // its diagonal is a constraint.
    TerrainFlipInput quad;
    quad.vertices = {{{0, 0, 1}, true, 1}, {{2, 0, 1}, true, 1}, {{2, 2, 1}, true, 1},
                     {{0, 2, 0}, false, 2}};
    quad.triangles = {{0, 1, 2}, {0, 2, 3}};
    const auto flippedQuad = flipTerrainFlatTriangles(quad);
    check(flippedQuad.ok && flippedQuad.report.flipsApplied == 1 &&
              flippedQuad.report.flatTrianglesRemaining == 0 &&
              flippedQuad.triangles ==
                  std::vector<std::array<std::uint32_t, 3>>{{0, 1, 3}, {1, 2, 3}},
          "a flat triangle is flipped away");
    quad.constraints = {{0, 2}};
    const auto constrained = flipTerrainFlatTriangles(quad);
    check(constrained.ok && constrained.report.flipsApplied == 0 &&
              constrained.report.flatTrianglesRemaining == 1,
          "a constraint diagonal is never flipped");
    quad.constraints.clear();
    quad.triangles = {{0, 2, 1}, {0, 2, 3}};
    check(!flipTerrainFlatTriangles(quad).ok, "clockwise flip input is refused");
}
void generatedCavities(int count) {
    Random random(20261020);
    std::size_t accepted = 0, refused = 0;
    std::vector<std::vector<std::uint8_t>> corpus;
    for (const auto &c : {cavityGrid(7, 3, 3, 1.6), cavityGrid(6, 2.5, 2.5, 1.2)})
        corpus.push_back(fuzz::encodeCavityCase(c));
    for (int i = 0; i < count; ++i) {
        fuzz::CavityCase c;
        auto coordinate = [&] { return double(random.pick(9 * 16)) / 16; };
        c.flags = std::uint8_t(random.pick(4));
        const int n = 4 + random.pick(40);
        const bool lattice = random.pick(2) == 0;
        for (int k = 0; k < n; ++k)
            c.base.push_back(lattice ? TerrainVec3{double(k % 7), double(k / 7), double(random.pick(4))}
                                     : TerrainVec3{coordinate(), coordinate(), double(random.pick(4))});
        c.cx = coordinate();
        c.cy = coordinate();
        c.radius = 0.5 + random.pick(5);
        c.stride = std::uint8_t(random.pick(4));
        for (int k = random.pick(3); k > 0; --k) {
            TerrainContour contour;
            contour.z = double(random.pick(4));
            contour.closed = random.pick(3) == 0;
            for (int m = 2 + random.pick(4); m > 0; --m)
                contour.points.push_back({coordinate(), coordinate()});
            c.contours.push_back(contour);
        }
        for (int k = random.pick(3); k > 0; --k) {
            TerrainBreakline line;
            for (int m = 2 + random.pick(3); m > 0; --m)
                line.points.push_back({coordinate(), coordinate(), double(random.pick(4))});
            c.breaklines.push_back(line);
        }
        for (int k = random.pick(4); k > 0; --k)
            c.points.push_back({{coordinate(), coordinate(), double(random.pick(4))}, 50});
        auto bytes = fuzz::encodeCavityCase(c);
        if (i % 3 == 2) {
            bytes = corpus[std::size_t(random.pick(int(corpus.size())))];
            for (int k = 1 + random.pick(4); k > 0; --k)
                bytes[std::size_t(random.pick(int(bytes.size())))] = std::uint8_t(random.next());
        }
        try {
            (fuzz::runCavityHarness(bytes.data(), bytes.size()) ? accepted : refused)++;
        } catch (const std::exception &) {
            std::cout << "failing cavity input " << i << ":";
            for (const auto b : bytes)
                std::printf(" %02x", unsigned(b));
            std::cout << "\n";
            throw;
        }
    }
    std::cout << "generated cavities: " << accepted << " re-triangulated, " << refused
              << " refused or empty\n";
    check(accepted > std::size_t(count) / 20, "a fair share of generated cavities is accepted");
}

int main(int argc, char **argv) {
    int fuzzCount = 3000;
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--fuzz-count"))
            fuzzCount = std::atoi(argv[i + 1]);
    try {
        const auto start = std::chrono::steady_clock::now();
        simpleContours();
        breakline();
        boundaryAndHole();
        constraintsOnBoundary();
        crossingRefused();
        conflictingDuplicates();
        insufficientPoints();
        inputValidation();
        flatFlip();
        steinerFill();
        maxEdge();
        determinism();
        budget();
        utmScale();
        reportBounds();
        boundaryAreaProperty();
        generatedInputs(fuzzCount);
        cavityBasics();
        cavityRefusals();
        cavityFlats();
        generatedCavities(fuzzCount);
        progressAndCancel();
        std::cout << "terrain triangulation tests passed in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                  << " s\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
