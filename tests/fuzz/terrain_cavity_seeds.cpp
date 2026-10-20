// R096.c: deterministic seeds for the terrain-cavity fuzz target.
//   terrain_cavity_fuzz_seeds NEW_DIRECTORY
// Each seed is re-triangulated except the refusal seeds, which reach the report paths.
#include "terrain_cavity_harness.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace sketchy;
namespace {
std::vector<TerrainVec3> grid(int n, double slope = 0.25) {
    std::vector<TerrainVec3> points;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            points.push_back({double(i), double(j), slope * i + 0.5 * j});
    return points;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: terrain_cavity_fuzz_seeds NEW_DIRECTORY");
        const std::filesystem::path root = argv[1];
        if (std::filesystem::exists(root) || !std::filesystem::create_directories(root))
            throw std::runtime_error("Seed output must be a new private directory");
        std::map<std::string, fuzz::CavityCase> seeds;
        {
            fuzz::CavityCase c; // a grid cavity with a new breakline and a spot point
            c.base = grid(7);
            c.cx = 3;
            c.cy = 3;
            c.radius = 1.6;
            c.breaklines = {TerrainBreakline{{{2.5, 2.25, 2.5}, {3.5, 3.6, 3.5}}, false, 7}};
            c.points = {{{3.25, 2.75, 3}, 8}};
            seeds["grid-breakline"] = c;
            c.stride = 3; // the breakline now crosses a kept constraint
            seeds["refused-crossing-kept"] = c;
            c.breaklines.clear();
            c.points = {{{3.3, 2.6, 3}, 8}};
            seeds["grid-constraints-point"] = c;
        }
        {
            fuzz::CavityCase c; // a closed contour inside, contour vertices flat around it
            c.flags = 2;
            c.base = grid(9, 0);
            c.cx = 4;
            c.cy = 4;
            c.radius = 2.6;
            c.contours = {TerrainContour{1, {{3.5, 3.5}, {4.5, 3.5}, {4.5, 4.5}, {3.5, 4.5}}, true, 9}};
            seeds["flat-ring"] = c;
            c.flags = 3;
            seeds["flat-ring-report-only"] = c;
        }
        {
            fuzz::CavityCase c; // a new vertex merging into a kept one with the same elevation
            c.base = grid(6);
            c.cx = 2.5;
            c.cy = 2.5;
            c.radius = 1.2;
            c.points = {{{2, 2, 0.25 * 2 + 0.5 * 2}, 5}};
            seeds["merge-kept"] = c;
        }
        {
            fuzz::CavityCase c; // refused: a new vertex on a kept one at another elevation
            c.base = grid(6);
            c.cx = 2.5;
            c.cy = 2.5;
            c.radius = 1.2;
            c.points = {{{2, 2, 9}, 5}};
            seeds["refused-duplicate"] = c;
        }
        {
            fuzz::CavityCase c; // refused: crossing new segments
            c.base = grid(8);
            c.cx = 3.5;
            c.cy = 3.5;
            c.radius = 2.5;
            c.breaklines = {TerrainBreakline{{{2.5, 2.5, 1}, {4.5, 4.5, 1}}, false, 1},
                            TerrainBreakline{{{2.5, 4.5, 1}, {4.5, 2.5, 1}}, false, 2}};
            seeds["refused-crossing"] = c;
        }
        {
            fuzz::CavityCase c; // refused: a new point outside the cavity
            c.base = grid(6);
            c.cx = 1;
            c.cy = 1;
            c.radius = 0.8;
            c.points = {{{4.25, 4.25, 1}, 5}};
            seeds["refused-outside"] = c;
        }
        for (const auto &[name, c] : seeds) {
            auto bytes = fuzz::encodeCavityCase(c);
            if (bytes.size() > 4096)
                throw std::runtime_error("bounded seed");
            const bool accepted = fuzz::runCavityHarness(bytes.data(), bytes.size());
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
