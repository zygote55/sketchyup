#pragma once
// R095.b (ADR 0168 section 3): the `terrain-triangulation` fuzz input is a compact binary
// description of one adapter call. Shared by the libFuzzer target, the seed generator and
// the in-ctest terrain_triangulation_tests. Standard library only.
//
// Layout (little endian; coordinates are int16 in sixteenths of a metre, so a small input
// reaches duplicates, collinear points and co-circular ties often):
//   u8  flags     bit0 Steiner fill, bit1 report-only flats, bit2 boundary, bit3 edge limit
//   u8  contours  (at most 8)      u8 breaklines (at most 8)
//   u8  holes     (at most 4)      u8 points (at most 255)
//   [u16 maximum edge length in sixteenths, when bit3]
//   per contour:  u8 count (at most 64), u8 closed (bit0), i16 z, count x (i16 x, i16 y)
//   per breakline: u8 count, u8 closed, count x (i16 x, i16 y, i16 z)
//   points:       count x (i16 x, i16 y, i16 z)
//   boundary (when bit2): u8 count, count x (i16 x, i16 y)
//   per hole:     u8 count, count x (i16 x, i16 y)
// A truncated input decodes to what it holds. Every byte string is a valid input.
#include "geometry/terrain_triangulation.hpp"
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace sketchy::fuzz {

class TerrainHarnessFailure : public std::logic_error {
    using std::logic_error::logic_error;
};
inline void terrainRequire(bool value, const char *message) {
    if (!value)
        throw TerrainHarnessFailure(message);
}

struct TerrainCase {
    TerrainTriangulationInput input;
    TerrainTriangulationOptions options;
};

inline constexpr double terrainFuzzScale = 16.0;

class TerrainReader {
  public:
    TerrainReader(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}
    bool done() const { return at_ >= size_; }
    std::uint8_t u8() { return at_ < size_ ? data_[at_++] : std::uint8_t(0); }
    double coordinate() {
        const std::uint16_t low = u8();
        const std::uint16_t high = u8();
        return double(std::int16_t(std::uint16_t(low | (high << 8)))) / terrainFuzzScale;
    }
    bool has(std::size_t bytes) const { return at_ + bytes <= size_; }

  private:
    const std::uint8_t *data_;
    std::size_t size_, at_{0};
};

inline TerrainCase decodeTerrainCase(const std::uint8_t *data, std::size_t size) {
    TerrainCase c;
    TerrainReader in(data, size);
    const std::uint8_t flags = in.u8();
    const std::size_t contours = std::min<std::size_t>(in.u8(), 8);
    const std::size_t breaklines = std::min<std::size_t>(in.u8(), 8);
    const std::size_t holes = std::min<std::size_t>(in.u8(), 4);
    const std::size_t points = in.u8();
    c.options.steinerFill = flags & 1;
    c.options.flatMode = (flags & 2) ? TerrainFlatMode::ReportOnly : TerrainFlatMode::Flip;
    if (flags & 8) {
        const double length = in.coordinate();
        if (std::isfinite(length) && length > 0)
            c.options.maxEdgeLength = length;
    }
    std::uint64_t id = 1;
    for (std::size_t i = 0; i < contours && in.has(2); ++i) {
        TerrainContour contour;
        const std::size_t count = std::min<std::size_t>(in.u8(), 64);
        contour.closed = in.u8() & 1;
        contour.z = in.coordinate();
        contour.sourceId = id++;
        for (std::size_t k = 0; k < count && in.has(4); ++k) {
            const double x = in.coordinate();
            contour.points.push_back({x, in.coordinate()});
        }
        c.input.contours.push_back(std::move(contour));
    }
    for (std::size_t i = 0; i < breaklines && in.has(2); ++i) {
        TerrainBreakline line;
        const std::size_t count = std::min<std::size_t>(in.u8(), 64);
        line.closed = in.u8() & 1;
        line.sourceId = id++;
        for (std::size_t k = 0; k < count && in.has(6); ++k) {
            const double x = in.coordinate();
            const double y = in.coordinate();
            line.points.push_back({x, y, in.coordinate()});
        }
        c.input.breaklines.push_back(std::move(line));
    }
    for (std::size_t i = 0; i < points && in.has(6); ++i) {
        const double x = in.coordinate();
        const double y = in.coordinate();
        c.input.points.push_back({{x, y, in.coordinate()}, id++});
    }
    const auto polygon = [&]() {
        TerrainPolygon p;
        const std::size_t count = in.u8();
        p.sourceId = id++;
        for (std::size_t k = 0; k < count && in.has(4); ++k) {
            const double x = in.coordinate();
            p.points.push_back({x, in.coordinate()});
        }
        return p;
    };
    if (flags & 4)
        c.input.boundary = polygon();
    for (std::size_t i = 0; i < holes && in.has(1); ++i)
        c.input.holes.push_back(polygon());
    return c;
}

// The inverse of decodeTerrainCase for inputs on the sixteenth-of-a-metre grid (seeds).
inline std::vector<std::uint8_t> encodeTerrainCase(const TerrainCase &c) {
    std::vector<std::uint8_t> out;
    const auto u8 = [&](std::size_t v) { out.push_back(std::uint8_t(v)); };
    const auto coordinate = [&](double v) {
        const auto q = std::int16_t(std::lround(v * terrainFuzzScale));
        out.push_back(std::uint8_t(std::uint16_t(q) & 0xff));
        out.push_back(std::uint8_t(std::uint16_t(q) >> 8));
    };
    u8((c.options.steinerFill ? 1 : 0) | (c.options.flatMode == TerrainFlatMode::ReportOnly ? 2 : 0) |
       (c.input.boundary ? 4 : 0) | (c.options.maxEdgeLength ? 8 : 0));
    u8(c.input.contours.size());
    u8(c.input.breaklines.size());
    u8(c.input.holes.size());
    u8(c.input.points.size());
    if (c.options.maxEdgeLength)
        coordinate(*c.options.maxEdgeLength);
    for (const auto &contour : c.input.contours) {
        u8(contour.points.size());
        u8(contour.closed ? 1 : 0);
        coordinate(contour.z);
        for (const auto &p : contour.points) {
            coordinate(p.x);
            coordinate(p.y);
        }
    }
    for (const auto &line : c.input.breaklines) {
        u8(line.points.size());
        u8(line.closed ? 1 : 0);
        for (const auto &p : line.points) {
            coordinate(p.x);
            coordinate(p.y);
            coordinate(p.z);
        }
    }
    for (const auto &p : c.input.points) {
        coordinate(p.position.x);
        coordinate(p.position.y);
        coordinate(p.position.z);
    }
    const auto polygon = [&](const TerrainPolygon &p) {
        u8(p.points.size());
        for (const auto &q : p.points) {
            coordinate(q.x);
            coordinate(q.y);
        }
    };
    if (c.input.boundary)
        polygon(*c.input.boundary);
    for (const auto &h : c.input.holes)
        polygon(h);
    return out;
}

inline bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

inline bool sameResult(const TerrainTriangulationResult &a, const TerrainTriangulationResult &b) {
    if (a.ok != b.ok || a.triangles != b.triangles || a.vertices.size() != b.vertices.size() ||
        a.report.refusals.size() != b.report.refusals.size())
        return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i)
        if (!sameBits(a.vertices[i].x, b.vertices[i].x) || !sameBits(a.vertices[i].y, b.vertices[i].y) ||
            !sameBits(a.vertices[i].z, b.vertices[i].z))
            return false;
    for (std::size_t i = 0; i < a.report.refusals.size(); ++i) {
        const auto &l = a.report.refusals[i];
        const auto &r = b.report.refusals[i];
        if (l.kind != r.kind || l.count != r.count || l.message != r.message || l.locations.size() != r.locations.size())
            return false;
    }
    const auto &l = a.report;
    const auto &r = b.report;
    return l.flatTrianglesRemaining == r.flatTrianglesRemaining && l.flipsApplied == r.flipsApplied &&
           l.steinerPointsAdded == r.steinerPointsAdded && l.mergedDuplicates == r.mergedDuplicates &&
           sameBits(l.flatAreaRemaining, r.flatAreaRemaining);
}

// Invariants of any accepted or refused result.
inline void checkTerrainResult(const TerrainTriangulationResult &result, const TerrainTriangulationOptions &options) {
    const auto &report = result.report;
    for (const auto &problem : report.refusals) {
        terrainRequire(problem.count >= 1, "a listed problem has a count");
        terrainRequire(problem.locations.size() <= terrainMaxLocationsPerKind, "locations are bounded");
        terrainRequire(problem.locations.size() <= problem.count, "locations do not exceed the count");
        terrainRequire(!problem.message.empty() && problem.message.size() <= terrainMaxMessageBytes,
                       "messages are bounded");
        // The adapter checks constraints itself with exact predicates; the library's own
        // check firing, or the library failing, means that check missed something.
        terrainRequire(problem.kind != TerrainProblemKind::TriangulationFailed, "the library did not fail");
        terrainRequire(problem.message.find("library found") == std::string::npos, "no crossing slipped past the adapter");
        for (const auto &where : problem.locations)
            terrainRequire(std::isfinite(where.xy.x) && std::isfinite(where.xy.y), "locations are finite");
    }
    terrainRequire(report.flatLocations.size() <= terrainMaxLocationsPerKind, "flat locations are bounded");
    if (!result.ok) {
        terrainRequire(result.vertices.empty() && result.triangles.empty(), "a refusal returns no mesh");
        terrainRequire(!report.refusals.empty(), "a refusal says why");
        return;
    }
    terrainRequire(report.refusals.empty(), "an accepted result has no refusals");
    terrainRequire(!result.triangles.empty(), "an accepted result has triangles");
    terrainRequire(result.triangles.size() <= options.limits.maxTriangles, "the triangle budget holds");
    terrainRequire(report.triangleCount == result.triangles.size() && report.vertexCount == result.vertices.size(),
                   "report counts match the mesh");
    for (const auto &v : result.vertices)
        terrainRequire(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z), "finite vertices");
    std::vector<char> used(result.vertices.size(), 0);
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    const std::array<std::uint32_t, 3> *previous = nullptr;
    for (const auto &t : result.triangles) {
        for (auto v : t)
            terrainRequire(v < result.vertices.size(), "triangle indices are in range");
        terrainRequire(t[0] < t[1] && t[0] < t[2] && t[1] != t[2], "triangles are rotated to the smallest index");
        terrainRequire(!previous || *previous < t, "triangles are sorted and unique");
        previous = &t;
        const auto &a = result.vertices[t[0]];
        const auto &b = result.vertices[t[1]];
        const auto &c = result.vertices[t[2]];
        terrainRequire((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0, "triangles are counter-clockwise");
        for (int i = 0; i < 3; ++i) {
            used[t[i]] = 1;
            terrainRequire(++directed[{t[i], t[(i + 1) % 3]}] == 1, "no directed edge is used twice");
        }
    }
    for (char u : used)
        terrainRequire(u != 0, "every output vertex is used");
    terrainRequire(report.flatTrianglesRemaining <= report.triangleCount, "flat triangles are a subset");
    if (options.flatMode == TerrainFlatMode::Flip && !options.steinerFill)
        terrainRequire(report.flatTrianglesRemaining <= report.flatTrianglesInitial, "flips never add flats");
}

// One adapter call: decode, run twice, compare bit for bit, check the invariants.
// Returns whether the triangulation was accepted.
inline bool runTerrainHarness(const std::uint8_t *data, std::size_t size) {
    const auto c = decodeTerrainCase(data, size);
    const auto first = triangulateTerrain(c.input, c.options);
    checkTerrainResult(first, c.options);
    const auto second = triangulateTerrain(c.input, c.options);
    terrainRequire(sameResult(first, second), "the same input gives the same bits");
    return first.ok;
}

} // namespace sketchy::fuzz
