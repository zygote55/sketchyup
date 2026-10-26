// R101.a timings against ADR 0175 §12, on the largest fixtures the engine's limits allow: a
// 250,000-triangle terrain (the terrain limit) and placements of a 12-face part (the ADR's
// quantities-b shape). Not a test: build with `cmake --build <dir> --target report_benchmark`
// and run it. The build type is part of the result; the ADR notes say which one was measured.
//
//   report_benchmark [placements]      default 10,000 placements, 250,000 terrain triangles
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/report.hpp"
#include "core/selection.hpp"
#include "core/terrain.hpp"
#include "core/terrain_records.hpp"
#include "core/transform.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using namespace sketchy;

namespace {

Id box(Document &doc, Vec3 origin, Vec3 size) {
    const auto x = origin.x + size.x, y = origin.y + size.y;
    const auto id =
        doc.addFace({{origin, {x, origin.y, origin.z}, {x, y, origin.z}, {origin.x, y, origin.z}}});
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, size.z);
    return id;
}

// A flat grid of n × n cells, two triangles each: 2n² triangles at z = 0.
TerrainMesh flatGrid(int n) {
    TerrainMesh mesh;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            mesh.vertices.push_back({double(i), double(j), 0});
    auto at = [&](int i, int j) { return std::uint32_t(j * (n + 1) + i); };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}

double seconds(std::chrono::steady_clock::duration span) {
    return std::chrono::duration<double>(span).count();
}

void timed(const char *label, const Document &doc, ReportRequest request) {
    const auto start = std::chrono::steady_clock::now();
    const auto result = runReport(doc, request);
    std::cout << label << ": " << seconds(std::chrono::steady_clock::now() - start) << " s";
    if (!result.counts.empty())
        std::cout << " (" << result.counts.front().contexts << " contexts)";
    std::cout << '\n';
}

} // namespace

int main(int argc, char **argv) {
    const auto placements = argc > 1 ? std::size_t(std::atoll(argv[1])) : std::size_t(10000);
    Document doc;
    const auto build = std::chrono::steady_clock::now();
    const auto mesh = flatGrid(353); // 249,218 triangles, inside the 250,000 terrain limit
    createTerrain(doc, mesh, [&] {
        TerrainRecord record;
        record.generator = "benchmark/terrain-v1";
        record.parameters.maxEdgeLength = 3;
        TerrainSource contour;
        contour.role = TerrainSourceRole::Contour;
        contour.reference = "benchmark";
        contour.points = {mesh.vertices.front(), mesh.vertices.back()};
        record.sources.push_back(contour);
        return record;
    }());
    std::cout << "terrain built in " << seconds(std::chrono::steady_clock::now() - build) << " s\n";

    // The part: two 6-face boxes in one group, made a component (the source placement is removed).
    const auto first = box(doc, {0, 0, 0}, {.5, .5, 2});
    const auto second = box(doc, {1, 0, 0}, {.5, .5, 2});
    const auto group = createGroup(doc, {first, second}, "Part");
    const auto definition = createComponent(doc, group, "Part").definition;
    Selection origin;
    origin.apply(doc, {{doc.instances().begin()->first, SelectionKind::Body, 0}},
                 SelectionMode::Replace);
    eraseSelected(doc, origin);
    const auto placing = std::chrono::steady_clock::now();
    const std::size_t columns = 100;
    for (std::size_t i = 0; i < placements; ++i)
        placeComponent(
            doc, definition,
            Transform::translation({double(i % columns) * 2, double(i / columns) * 2 + 10, 0}));
    std::cout << placements << " placements in "
              << seconds(std::chrono::steady_clock::now() - placing) << " s; "
              << doc.bodies().size() << " records\n";

    ReportRequest counts;
    counts.areas = counts.volumes = false;
    timed("counts (groupBy none)", doc, counts);
    ReportRequest areas;
    areas.counts = areas.volumes = false;
    timed("areas", doc, areas);
    ReportRequest volumes;
    volumes.counts = volumes.areas = false;
    timed("solid volumes", doc, volumes);
    timed("all tables", doc, ReportRequest{});
    // Peak resident memory of this process (Linux), the ADR's engine memory budget.
    std::ifstream status("/proc/self/status");
    for (std::string line; std::getline(status, line);)
        if (line.rfind("VmHWM", 0) == 0)
            std::cout << line << '\n';
    return 0;
}
