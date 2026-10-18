// R095.b: deterministic valid seeds for the terrain-triangulation fuzz target.
//   terrain_triangulation_fuzz_seeds NEW_DIRECTORY
// Each seed is accepted by the adapter except the refusal seeds, which reach the report paths.
#include "terrain_triangulation_harness.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

using namespace sketchy;
namespace {

TerrainContour ring(double z, double radius, int n, std::uint64_t id) {
    TerrainContour c;
    c.z = z;
    c.closed = true;
    c.sourceId = id;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * M_PI * i / n;
        c.points.push_back({std::round(radius * std::cos(a) * 16) / 16,
                            std::round(radius * std::sin(a) * 16) / 16});
    }
    return c;
}
TerrainTriangulationInput grid(int n) {
    TerrainTriangulationInput in;
    std::uint64_t id = 1;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            in.points.push_back({{double(i), double(j), 0.25 * i + 0.5 * j}, id++});
    return in;
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: terrain_triangulation_fuzz_seeds NEW_DIRECTORY");
        const std::filesystem::path root = argv[1];
        if (std::filesystem::exists(root) || !std::filesystem::create_directories(root))
            throw std::runtime_error("Seed output must be a new private directory");
        std::map<std::string, fuzz::TerrainCase> seeds;
        {
            fuzz::TerrainCase c;
            c.input.contours = {ring(0, 8, 16, 1), ring(1, 5, 12, 2), ring(2, 2, 8, 3)};
            seeds["hill"] = c;
            c.options.steinerFill = true;
            seeds["hill-steiner"] = c;
        }
        {
            fuzz::TerrainCase c;
            c.input.contours = {TerrainContour{0, {{0, 0}, {2, 3}, {4, 0}}, false, 1}};
            c.input.points = {{{2, -3, 1}, 2}};
            seeds["tent-flip"] = c;
            c.options.flatMode = TerrainFlatMode::ReportOnly;
            seeds["tent-report-only"] = c;
        }
        {
            fuzz::TerrainCase c;
            c.input.contours = {TerrainContour{0, {{0, 0}, {6, 1}, {4, 0}}, false, 1}};
            c.input.points = {{{2, -3, 1}, 2}};
            c.options.steinerFill = true;
            seeds["bent-tent-steiner"] = c;
        }
        {
            fuzz::TerrainCase c;
            c.input = grid(7);
            c.input.breaklines = {
                TerrainBreakline{{{0.5, 0.5, 3}, {3.5, 2.5, 4}, {2.5, 5.25, 5}}, false, 90}};
            seeds["grid-breakline"] = c;
        }
        {
            fuzz::TerrainCase c;
            c.input = grid(9);
            c.input.boundary = TerrainPolygon{{{1, 1}, {7, 1}, {7, 7}, {1, 7}}, 91};
            c.input.holes = {TerrainPolygon{{{3, 3}, {5, 3}, {5, 5}, {3, 5}}, 92}};
            seeds["grid-boundary-hole"] = c;
            c.input.boundary = TerrainPolygon{{{0.5, 0.5}, {7.5, 0.5}, {7.5, 7.5}, {0.5, 7.5}}, 93};
            seeds["grid-boundary-interpolated"] = c;
        }
        {
            fuzz::TerrainCase c;
            c.input = grid(5);
            c.input.points.push_back({{20, 0, 1}, 50});
            c.input.points.push_back({{21, 1, 1}, 51});
            c.input.points.push_back({{20, 2, 1}, 52});
            c.options.maxEdgeLength = 3;
            seeds["trim-edge-length"] = c;
        }
        {
            fuzz::TerrainCase c; // refused: crossing contours
            c.input.contours = {TerrainContour{1, {{0, 0}, {4, 4}}, false, 1},
                                TerrainContour{2, {{0, 4}, {4, 0}}, false, 2}};
            c.input.points = {{{9, 9, 0}, 3}};
            seeds["refused-crossing"] = c;
        }
        {
            fuzz::TerrainCase c; // refused: same x and y, different z
            c.input = grid(3);
            c.input.points.push_back({{1, 1, 9}, 77});
            seeds["refused-duplicate"] = c;
        }
        {
            fuzz::TerrainCase c; // refused: collinear
            for (int i = 0; i < 5; ++i)
                c.input.points.push_back({{double(i), double(i), 0}, std::uint64_t(i + 1)});
            seeds["refused-collinear"] = c;
        }
        for (const auto &[name, c] : seeds) {
            auto bytes = fuzz::encodeTerrainCase(c);
            if (bytes.size() > 8192)
                throw std::runtime_error("bounded seed");
            const bool accepted = fuzz::runTerrainHarness(bytes.data(), bytes.size());
            if (accepted == (name.rfind("refused-", 0) == 0))
                throw std::runtime_error("seed " + name + " has the wrong outcome");
            std::ofstream out(root / (name + ".bin"), std::ios::binary);
            out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
            std::cout << name << ' ' << bytes.size() << " bytes, "
                      << (accepted ? "accepted" : "refused") << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
