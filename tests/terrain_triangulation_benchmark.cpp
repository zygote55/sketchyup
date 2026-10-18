// R095.b opt-in timing run (ADR 0168 section 6: parse and triangulate within 3 s at 250k
// triangles). Build with optimization:
//   cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
//         -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=OFF
//   cmake --build build/bench --target terrain_triangulation_benchmark
//   build/bench/terrain_triangulation_benchmark
// Three terrains of about 10k, 100k and 250k triangles, each as scattered points and as
// concentric contours with every segment a constraint, plus each contour terrain with Steiner
// fill on, and an 8:1 elongated version. Times are best of three, in milliseconds, for the
// whole adapter call (merge, constraint checks, triangulation, flips, output).
#include "geometry/terrain_triangulation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace sketchy;
namespace {

std::uint64_t state = 12345;
double unit() {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return double((z ^ (z >> 31)) >> 11) / double(1ull << 53);
}

// About `triangles` triangles: a Delaunay triangulation of n points has about 2n of them.
TerrainTriangulationInput scattered(std::size_t triangles) {
    TerrainTriangulationInput in;
    for (std::size_t i = 0; i < triangles / 2; ++i) {
        const double x = unit() * 2000, y = unit() * 2000;
        in.points.push_back({{x, y, 100 + 30 * std::sin(x / 200) * std::cos(y / 170)}, i + 1});
    }
    return in;
}
// Concentric closed contours, 500 vertices each, ring spacing 1.5 m, stretched by `aspect`
// along x (8 gives the elongated hill that makes about 5% flat triangles).
TerrainTriangulationInput contours(std::size_t triangles, double aspect = 1) {
    TerrainTriangulationInput in;
    const std::size_t perRing = 500;
    const std::size_t rings = std::max<std::size_t>(2, triangles / (2 * perRing));
    for (std::size_t r = 0; r < rings; ++r) {
        TerrainContour c;
        c.closed = true;
        c.z = double(r);
        c.sourceId = r + 1;
        const double radius = double(rings - r) * 1.5 + 2;
        for (std::size_t i = 0; i < perRing; ++i) {
            const double a = 2 * M_PI * double(i) / double(perRing);
            c.points.push_back({aspect * radius * std::cos(a), radius * std::sin(a)});
        }
        in.contours.push_back(std::move(c));
    }
    return in;
}

void measure(const char *name, const TerrainTriangulationInput &in, const TerrainTriangulationOptions &options) {
    double best = 1e300;
    TerrainTriangulationResult result;
    for (int run = 0; run < 3; ++run) {
        const auto start = std::chrono::steady_clock::now();
        result = triangulateTerrain(in, options);
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::printf("%-28s %8zu triangles %8zu vertices %9.1f ms  flats %llu->%llu  %s\n", name, result.triangles.size(),
                result.vertices.size(), best, (unsigned long long)result.report.flatTrianglesInitial,
                (unsigned long long)result.report.flatTrianglesRemaining, result.ok ? "ok" : "REFUSED");
}

} // namespace

int main() {
    TerrainTriangulationOptions defaults, fill;
    fill.steinerFill = true;
    for (std::size_t target : {10000u, 100000u, 249000u}) {
        char name[64];
        std::snprintf(name, sizeof name, "points %zuk", target / 1000);
        measure(name, scattered(target), defaults);
        std::snprintf(name, sizeof name, "contours %zuk", target / 1000);
        measure(name, contours(target), defaults);
        std::snprintf(name, sizeof name, "contours+steiner %zuk", target / 1000);
        measure(name, contours(target), fill);
        std::snprintf(name, sizeof name, "elongated 8:1 %zuk", target / 1000);
        measure(name, contours(target, 8), defaults);
        std::snprintf(name, sizeof name, "elongated 8:1 +steiner %zuk", target / 1000);
        measure(name, contours(target, 8), fill);
    }
}
