#pragma once
// R096.c (ADR 0169 §3, §9 "Cavity adapter"): the `terrain-cavity` fuzz input describes a base
// point set, a cavity in its triangulation and the geometry inserted into it. Shared by the
// libFuzzer target parser_fuzz_terrain_cavity, its seed generator and terrain_triangulation_tests.
// Standard library only.
//
// Layout (little endian; coordinates are int16 in sixteenths of a metre, as the
// terrain-triangulation format, so ties, collinear points and duplicates are common):
//   u8  flags        bit0 report-only flats, bit1 base points are contour vertices
//   u8  base points  (3 to 96), then that many (i16 x, i16 y, i16 z)
//   i16 cx, i16 cy, u16 radius   the cavity: base triangles whose centroid is within radius
//   u8  constraint stride        every n-th interior cavity edge is a kept constraint (0: none)
//   u8  contours (at most 4), u8 breaklines (at most 4), u8 points (at most 16)
//   per contour:  u8 count (at most 16), u8 closed (bit0), i16 z, count x (i16 x, i16 y)
//   per breakline: u8 count, u8 closed, count x (i16 x, i16 y, i16 z)
//   points:       count x (i16 x, i16 y, i16 z)
// A truncated input decodes to what it holds. Every byte string is a valid input.
//
// The harness triangulates the base, cuts the cavity out, runs retriangulateTerrainCavity twice
// (bit for bit), once more with the kept vertices in reverse order (the same triangles by
// position: the input is canonicalised), and checks the result: counter-clockwise triangles,
// every boundary edge exactly once in its direction, no directed edge twice, the cavity's area
// kept, every constraint and new segment an edge, every new vertex used.
#include "terrain_triangulation_harness.hpp"
#include <optional>
#include <set>

namespace sketchy::fuzz {

struct CavityCase {
    std::uint8_t flags{};
    std::vector<TerrainVec3> base;
    double cx{}, cy{}, radius{};
    std::uint8_t stride{};
    std::vector<TerrainContour> contours;
    std::vector<TerrainBreakline> breaklines;
    std::vector<TerrainPoint> points;
};

inline CavityCase decodeCavityCase(const std::uint8_t *data, std::size_t size) {
    CavityCase c;
    TerrainReader in(data, size);
    c.flags = in.u8();
    const std::size_t base = std::min<std::size_t>(in.u8(), 96);
    for (std::size_t i = 0; i < base && in.has(6); ++i) {
        const double x = in.coordinate(), y = in.coordinate();
        c.base.push_back({x, y, in.coordinate()});
    }
    c.cx = in.coordinate();
    c.cy = in.coordinate();
    {
        const std::uint16_t low = in.u8(), high = in.u8();
        c.radius = double(std::uint16_t(low | (high << 8))) / terrainFuzzScale;
    }
    c.stride = in.u8();
    const std::size_t contours = std::min<std::size_t>(in.u8(), 4);
    const std::size_t breaklines = std::min<std::size_t>(in.u8(), 4);
    const std::size_t points = std::min<std::size_t>(in.u8(), 16);
    std::uint64_t id = 1000;
    for (std::size_t i = 0; i < contours && in.has(2); ++i) {
        TerrainContour contour;
        const std::size_t count = std::min<std::size_t>(in.u8(), 16);
        contour.closed = in.u8() & 1;
        contour.z = in.coordinate();
        contour.sourceId = id++;
        for (std::size_t k = 0; k < count && in.has(4); ++k) {
            const double x = in.coordinate();
            contour.points.push_back({x, in.coordinate()});
        }
        c.contours.push_back(std::move(contour));
    }
    for (std::size_t i = 0; i < breaklines && in.has(2); ++i) {
        TerrainBreakline line;
        const std::size_t count = std::min<std::size_t>(in.u8(), 16);
        line.closed = in.u8() & 1;
        line.sourceId = id++;
        for (std::size_t k = 0; k < count && in.has(6); ++k) {
            const double x = in.coordinate(), y = in.coordinate();
            line.points.push_back({x, y, in.coordinate()});
        }
        c.breaklines.push_back(std::move(line));
    }
    for (std::size_t i = 0; i < points && in.has(6); ++i) {
        const double x = in.coordinate(), y = in.coordinate();
        c.points.push_back({{x, y, in.coordinate()}, id++});
    }
    return c;
}

inline std::vector<std::uint8_t> encodeCavityCase(const CavityCase &c) {
    std::vector<std::uint8_t> out;
    const auto u8 = [&](std::size_t v) { out.push_back(std::uint8_t(v)); };
    const auto coordinate = [&](double v) {
        const auto q = std::int16_t(std::lround(v * terrainFuzzScale));
        out.push_back(std::uint8_t(std::uint16_t(q) & 0xff));
        out.push_back(std::uint8_t(std::uint16_t(q) >> 8));
    };
    u8(c.flags);
    u8(c.base.size());
    for (const auto &p : c.base) {
        coordinate(p.x);
        coordinate(p.y);
        coordinate(p.z);
    }
    coordinate(c.cx);
    coordinate(c.cy);
    const auto r = std::uint16_t(std::lround(c.radius * terrainFuzzScale));
    out.push_back(std::uint8_t(r & 0xff));
    out.push_back(std::uint8_t(r >> 8));
    u8(c.stride);
    u8(c.contours.size());
    u8(c.breaklines.size());
    u8(c.points.size());
    for (const auto &contour : c.contours) {
        u8(contour.points.size());
        u8(contour.closed ? 1 : 0);
        coordinate(contour.z);
        for (const auto &p : contour.points) {
            coordinate(p.x);
            coordinate(p.y);
        }
    }
    for (const auto &line : c.breaklines) {
        u8(line.points.size());
        u8(line.closed ? 1 : 0);
        for (const auto &p : line.points) {
            coordinate(p.x);
            coordinate(p.y);
            coordinate(p.z);
        }
    }
    for (const auto &p : c.points) {
        coordinate(p.position.x);
        coordinate(p.position.y);
        coordinate(p.position.z);
    }
    return out;
}

inline double twiceArea(const TerrainVec3 &a, const TerrainVec3 &b, const TerrainVec3 &c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// The cavity input of a case, or nothing when the base does not triangulate or the cavity is
// empty. `reverse` lists the kept vertices in reverse order.
struct CavitySetup {
    TerrainCavityInput input;
    double area{}; // twice the cavity's plan area
};
inline std::optional<CavitySetup> cavitySetup(const CavityCase &c, bool reverse) {
    TerrainTriangulationInput base;
    std::uint64_t id = 1;
    for (const auto &p : c.base)
        base.points.push_back({p, id++});
    const auto triangulated = triangulateTerrain(base);
    if (!triangulated.ok)
        return std::nullopt;
    const auto &v = triangulated.vertices;
    std::vector<std::array<std::uint32_t, 3>> cavity;
    for (const auto &t : triangulated.triangles) {
        const double x = (v[t[0]].x + v[t[1]].x + v[t[2]].x) / 3,
                     y = (v[t[0]].y + v[t[1]].y + v[t[2]].y) / 3;
        if (std::hypot(x - c.cx, y - c.cy) <= c.radius)
            cavity.push_back(t);
    }
    if (cavity.empty())
        return std::nullopt;
    CavitySetup setup;
    std::map<std::uint32_t, std::uint32_t> index;
    std::vector<std::uint32_t> used;
    for (const auto &t : cavity)
        for (const auto i : t)
            if (index.emplace(i, 0).second)
                used.push_back(i);
    std::sort(used.begin(), used.end());
    if (reverse)
        std::reverse(used.begin(), used.end());
    for (std::size_t k = 0; k < used.size(); ++k) {
        index[used[k]] = std::uint32_t(k);
        setup.input.vertices.push_back({v[used[k]], (c.flags & 2) != 0, used[k] + 1});
    }
    // Edges by base vertex index, so the constraints chosen do not depend on `reverse`.
    std::set<std::pair<std::uint32_t, std::uint32_t>> directed;
    for (const auto &t : cavity) {
        setup.area += twiceArea(v[t[0]], v[t[1]], v[t[2]]);
        for (int i = 0; i < 3; ++i)
            directed.insert({t[std::size_t(i)], t[std::size_t((i + 1) % 3)]});
    }
    std::size_t interior = 0;
    for (const auto &[a, b] : directed) {
        if (!directed.contains({b, a}))
            setup.input.boundary.push_back({index[a], index[b]});
        else if (a < b && c.stride && interior++ % c.stride == 0)
            setup.input.constraints.push_back({index[a], index[b]});
    }
    setup.input.contours = c.contours;
    setup.input.breaklines = c.breaklines;
    setup.input.points = c.points;
    return setup;
}

inline bool sameCavityResult(const TerrainCavityResult &a, const TerrainCavityResult &b) {
    if (a.ok != b.ok || a.triangles != b.triangles || a.placed != b.placed ||
        a.vertices.size() != b.vertices.size() ||
        a.report.refusals.size() != b.report.refusals.size())
        return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i)
        if (!sameBits(a.vertices[i].x, b.vertices[i].x) ||
            !sameBits(a.vertices[i].y, b.vertices[i].y) ||
            !sameBits(a.vertices[i].z, b.vertices[i].z))
            return false;
    for (std::size_t i = 0; i < a.report.refusals.size(); ++i)
        if (a.report.refusals[i].kind != b.report.refusals[i].kind ||
            a.report.refusals[i].count != b.report.refusals[i].count)
            return false;
    return a.report.flipsApplied == b.report.flipsApplied &&
           a.report.flatTrianglesRemaining == b.report.flatTrianglesRemaining;
}

// The triangles as sorted position triples, for comparing results with different numbering.
inline std::vector<std::array<std::array<double, 3>, 3>>
positionTriangles(const TerrainCavityResult &r) {
    std::vector<std::array<std::array<double, 3>, 3>> out;
    for (const auto &t : r.triangles) {
        std::array<std::array<double, 3>, 3> tri{};
        for (int i = 0; i < 3; ++i) {
            const auto &p = r.vertices[t[std::size_t(i)]];
            tri[std::size_t(i)] = {p.x, p.y, p.z};
        }
        std::rotate(tri.begin(), std::min_element(tri.begin(), tri.end()), tri.end());
        out.push_back(tri);
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline void checkCavityResult(const CavitySetup &setup, const TerrainCavityResult &result) {
    const auto &report = result.report;
    for (const auto &problem : report.refusals) {
        terrainRequire(problem.count >= 1, "a listed problem has a count");
        terrainRequire(problem.locations.size() <= terrainMaxLocationsPerKind,
                       "locations are bounded");
        terrainRequire(!problem.message.empty() && problem.message.size() <= terrainMaxMessageBytes,
                       "messages are bounded");
        terrainRequire(problem.kind != TerrainProblemKind::TriangulationFailed,
                       "the cavity's own checks catch what the library would refuse");
        terrainRequire(problem.message.find("library found") == std::string::npos,
                       "no crossing slipped past the adapter");
    }
    if (!result.ok) {
        terrainRequire(result.vertices.empty() && result.triangles.empty() && result.placed.empty(),
                       "a refusal returns no mesh");
        terrainRequire(!report.refusals.empty(), "a refusal says why");
        return;
    }
    const auto &input = setup.input;
    const auto &v = result.vertices;
    terrainRequire(v.size() >= input.vertices.size(), "the kept vertices come first");
    for (std::size_t i = 0; i < input.vertices.size(); ++i)
        terrainRequire(sameBits(v[i].x, input.vertices[i].position.x) &&
                           sameBits(v[i].y, input.vertices[i].position.y) &&
                           sameBits(v[i].z, input.vertices[i].position.z),
                       "the kept vertices are unchanged");
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    std::vector<char> used(v.size(), 0);
    double area = 0;
    const std::array<std::uint32_t, 3> *previous = nullptr;
    for (const auto &t : result.triangles) {
        for (auto i : t)
            terrainRequire(i < v.size(), "triangle indices are in range");
        terrainRequire(t[0] < t[1] && t[0] < t[2], "triangles start at their smallest index");
        terrainRequire(!previous || *previous < t, "triangles are sorted and unique");
        previous = &t;
        terrainRequire(terrainOrientation({v[t[0]].x, v[t[0]].y}, {v[t[1]].x, v[t[1]].y},
                                          {v[t[2]].x, v[t[2]].y}) > 0,
                       "triangles are counter-clockwise");
        area += twiceArea(v[t[0]], v[t[1]], v[t[2]]);
        for (int i = 0; i < 3; ++i) {
            used[t[std::size_t(i)]] = 1;
            terrainRequire(++directed[{t[std::size_t(i)], t[std::size_t((i + 1) % 3)]}] == 1,
                           "no directed edge is used twice");
        }
    }
    for (const auto &[a, b] : input.boundary)
        terrainRequire(directed.contains({a, b}), "every boundary edge is kept in its direction");
    std::size_t open = 0;
    for (const auto &[edge, count] : directed)
        open += !directed.contains({edge.second, edge.first});
    terrainRequire(open == input.boundary.size(), "the only open edges are the boundary's");
    for (const auto &[a, b] : input.constraints)
        terrainRequire(directed.contains({a, b}) || directed.contains({b, a}),
                       "every kept constraint is an edge");
    for (char u : used)
        terrainRequire(u != 0, "every vertex is used");
    terrainRequire(std::abs(area - setup.area) <= 1e-9 * std::max(1.0, std::abs(setup.area)),
                   "the cavity's area is kept");
    std::size_t news = 0;
    for (const auto &c : input.contours)
        news += c.points.size();
    for (const auto &b : input.breaklines)
        news += b.points.size();
    news += input.points.size();
    terrainRequire(result.placed.size() == news, "every new vertex is placed");
    // New segments are edges (zero-length ones dropped).
    std::size_t at = 0;
    const auto segments = [&](std::size_t count, bool closed) {
        const std::size_t last = closed && count > 2 ? count : count - (count ? 1 : 0);
        for (std::size_t i = 0; i < last; ++i) {
            const auto a = result.placed[at + i], b = result.placed[at + (i + 1) % count];
            if (a != b)
                terrainRequire(directed.contains({a, b}) || directed.contains({b, a}),
                               "every new segment is an edge");
        }
        at += count;
    };
    for (const auto &c : input.contours)
        segments(c.points.size(), c.closed);
    for (const auto &b : input.breaklines)
        segments(b.points.size(), b.closed);
}

// One case: run three times (twice as given, once reversed), compare, check. Returns whether
// the cavity was re-triangulated.
inline bool runCavityHarness(const std::uint8_t *data, std::size_t size) {
    const auto c = decodeCavityCase(data, size);
    const auto setup = cavitySetup(c, false);
    if (!setup)
        return false;
    TerrainTriangulationOptions options;
    options.flatMode = (c.flags & 1) ? TerrainFlatMode::ReportOnly : TerrainFlatMode::Flip;
    const auto first = retriangulateTerrainCavity(setup->input, options);
    checkCavityResult(*setup, first);
    const auto second = retriangulateTerrainCavity(setup->input, options);
    terrainRequire(sameCavityResult(first, second), "the same cavity gives the same bits");
    const auto reversed = cavitySetup(c, true);
    const auto third = retriangulateTerrainCavity(reversed->input, options);
    checkCavityResult(*reversed, third);
    terrainRequire(first.ok == third.ok, "the vertex order does not decide acceptance");
    if (first.ok)
        terrainRequire(positionTriangles(first) == positionTriangles(third),
                       "the input is canonicalised by position");
    return first.ok;
}

} // namespace sketchy::fuzz
