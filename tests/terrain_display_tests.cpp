// R095.e2 terrain display (ADR 0168 "Decisions > Interaction design", Appearance): contour lines
// derived from the mesh (counts and index lines on a known mesh, determinism, the cache key and
// the bound at 250,000 triangles), the display settings edit (one undoable edit that keeps the
// mesh digest, save and reopen, defaults omitted from the encoding, terrain.set_display), the
// "Terrain" material on new and regenerated terrains, and the surface data that hides tile seams
// and joins normals across them.
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/terrain_commands.hpp"
#include "core/materials.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "io/terrain_io.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejection(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
// A grid of nx x ny cells, two triangles each, with z = f(x, y).
TerrainMesh grid(int nx, int ny, double spacing, const std::function<double(double, double)> &z) {
    TerrainMesh mesh;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i)
            mesh.vertices.push_back({i * spacing, j * spacing, z(i * spacing, j * spacing)});
    auto at = [&](int i, int j) { return std::uint32_t(j * (nx + 1) + i); };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}
TerrainRecord record() {
    TerrainRecord result;
    result.generator = "test/terrain-v1";
    result.parameters.boundary = TerrainBoundaryMode::ConvexHull;
    return result;
}
std::vector<const Body *> tiles(const Document &doc, Id terrain) {
    std::vector<const Body *> result;
    for (const auto id : terrainTiles(doc, terrain))
        result.push_back(doc.bodies().at(id).get());
    return result;
}
std::string lastLabel(const Document &doc) {
    std::string label;
    for (const auto &entry : doc.history().entries)
        if (entry.applied)
            label = entry.label;
    return label;
}
// The document JSON of a container without its revision counters.
QJsonObject documentTree(const QByteArray &container) {
    auto tree = QJsonDocument::fromJson(encodeDocument(decodeContainer(container))).object();
    tree.remove("revision");
    return tree;
}
std::size_t historyTotal(const Document &doc) { return doc.history().total; }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}

void contoursOnKnownMesh() {
    // A ramp z = x over 10 x 2 one-metre cells: z runs 0..10.
    Document doc;
    const auto terrain =
        createTerrain(doc, grid(10, 2, 1, [](double x, double) { return x; }), record()).terrain;
    // Offset 0.5: levels cross each cell column once, through four triangles per level.
    const auto shifted = terrainContours(tiles(doc, terrain), 1, .5);
    check(!shifted.tooDense && shifted.levels == 10 && shifted.segments.size() == 40,
          "Ten levels of four segments on the shifted ramp, got " +
              std::to_string(shifted.levels) + " levels and " +
              std::to_string(shifted.segments.size()) + " segments");
    check(shifted.indexLevels == 2, "Every fifth level is an index line (elevations 5 and 10)");
    std::size_t index = 0;
    for (const auto &segment : shifted.segments) {
        // Each segment is level: e = z + 0.5 is a whole number.
        const auto e = segment.a.z + .5;
        check(std::abs(e - std::round(e)) < 1e-12 && segment.a.z == segment.b.z,
              "Segments lie on their level");
        check(segment.index == (std::llround(e) % 5 == 0), "Index flag follows the level");
        index += segment.index;
        check(std::abs(segment.a.x - segment.a.z) < 1e-12, "The level crosses the ramp at x = z");
    }
    check(index == 8, "Index segments: two levels of four");
    // Levels through vertices: a level touching a triangle at one vertex gives no segment; the
    // grid line at the level gives one segment per row.
    const auto exact = terrainContours(tiles(doc, terrain), 1, 0);
    check(exact.levels == 10 && exact.segments.size() == 20,
          "Levels through grid lines: one segment per row, got " +
              std::to_string(exact.segments.size()));
    for (const auto &segment : exact.segments)
        check(!(segment.a == segment.b), "No zero-length segments");
    // A coarser interval: levels 2, 4, … 10; only 10 is an index line (5 x 2).
    const auto coarse = terrainContours(tiles(doc, terrain), 2, .5);
    check(coarse.levels == 5 && coarse.indexLevels == 1, "Interval 2: five levels, one index");
    // Out of range intervals are refused.
    check(!rejection([&] { terrainContours(tiles(doc, terrain), .001, 0); }).empty() &&
              !rejection([&] { terrainContours(tiles(doc, terrain), 2000, 0); }).empty(),
          "Intervals outside 0.01 to 1,000 m are refused");
}

void determinism() {
    Document doc;
    const auto mesh =
        grid(80, 60, .5, [](double x, double y) { return 3 * std::sin(x * .2) * std::cos(y * .3); });
    const auto terrain = createTerrain(doc, mesh, record()).terrain;
    check(terrainTiles(doc, terrain).size() > 1, "The wavy terrain has several tiles");
    const auto a = terrainContours(tiles(doc, terrain), .25, 0);
    const auto b = terrainContours(tiles(doc, terrain), .25, 0);
    check(a.segments.size() == b.segments.size() && !a.segments.empty(), "Same counts");
    check(std::memcmp(a.segments.data(), b.segments.data(),
                      a.segments.size() * sizeof(TerrainContourSegment)) == 0 ||
              [&] {
                  for (std::size_t i = 0; i < a.segments.size(); ++i)
                      if (!(a.segments[i].a == b.segments[i].a) ||
                          !(a.segments[i].b == b.segments[i].b) ||
                          a.segments[i].index != b.segments[i].index)
                          return false;
                  return true;
              }(),
          "Contours are deterministic");
    // The same mesh in another document (other body IDs) gives the same segments.
    Document other;
    other.addFace({{{50, 50, 0}, {51, 50, 0}, {51, 51, 0}}});
    const auto copy = createTerrain(other, mesh, record()).terrain;
    const auto c = terrainContours(tiles(other, copy), .25, 0);
    check(c.segments.size() == a.segments.size(), "Another document gives the same contours");
    for (std::size_t i = 0; i < a.segments.size(); ++i)
        check(a.segments[i].a == c.segments[i].a && a.segments[i].b == c.segments[i].b,
              "Segment order and positions are deterministic");
    // Shared edges give bit-identical crossing points from both triangles: every interior
    // segment endpoint is shared by exactly two segments (a closed or boundary-ending chain).
    std::map<std::tuple<double, double, double>, int> uses;
    for (const auto &segment : a.segments)
        for (const auto point : {segment.a, segment.b})
            ++uses[{point.x, point.y, point.z}];
    std::size_t odd = 0;
    for (const auto &[point, count] : uses)
        odd += count % 2;
    check(odd < uses.size() / 10, "Neighbouring triangles meet at identical points");
}

void cacheKey() {
    Document doc;
    const auto terrain = createTerrain(
        doc, grid(20, 20, 1, [](double x, double y) { return .3 * x + .1 * y; }), record())
                             .terrain;
    TerrainContourCache cache;
    const auto first = cache.get(doc, terrain);
    const auto tiles = cache.tileGenerations();
    check(cache.generations() == 1 && tiles == terrainTiles(doc, terrain).size(),
          "First lookup generates every tile");
    check(cache.get(doc, terrain) == first && cache.generations() == 1 &&
              cache.tileGenerations() == tiles,
          "The same tiles and interval reuse the contours");
    // The interval is part of the key.
    auto display = doc.bodies().at(terrain)->terrain->display;
    display.contourInterval = .5;
    setTerrainDisplay(doc, terrain, display);
    const auto finer = cache.get(doc, terrain);
    check(finer != first && cache.generations() == 2 && finer->interval == .5,
          "A new interval regenerates");
    // Paint replaces a tile record but not the mesh: the tile's contours and the terrain's are
    // kept (R096.a: compared per tile by mesh).
    const auto tile = terrainTiles(doc, terrain).front();
    const auto generated = cache.tileGenerations();
    doc.paint(tile, {.9f, .1f, .1f});
    check(cache.get(doc, terrain) == finer && cache.generations() == 2 &&
              cache.tileGenerations() == generated,
          "Same mesh, same contours");
    // Moving the terrain up changes the site elevation of its frame: a new key.
    const auto before = doc.bodies().at(terrain);
    auto moved = std::make_shared<Body>(*before);
    moved->transform = Transform::translation({0, 0, .25}) * moved->transform;
    doc.apply({"Move", {{terrain, before, moved}}}, doc.revision());
    check(cache.get(doc, terrain) != finer && cache.generations() == 3,
          "The elevation offset is part of the key");
    // Bounded: entries for removed terrains are dropped, and capacity is enforced.
    TerrainContourCache small(2);
    std::vector<Id> terrains;
    for (int i = 0; i < 4; ++i)
        terrains.push_back(createTerrain(
                               doc, grid(2, 2, 1, [i](double x, double) { return x + i; }), record())
                               .terrain);
    for (const auto id : terrains)
        small.get(doc, id);
    check(small.size() == 2, "The cache keeps at most its capacity");
    explodeTerrain(doc, terrains.back());
    small.prune(doc);
    check(small.size() == 1, "Pruning drops terrains that no longer exist");
}

void bound() {
    // 249,924 triangles (353 x 354 cells), the document limit's scale. Steep enough that a
    // 1 cm interval would need millions of segments.
    // The tiles are built as createTerrain builds them, without a document: one edit of this
    // size is over the 64 MiB history budget (a document reaches it through several terrains).
    const auto mesh =
        grid(353, 354, 1, [](double x, double y) { return 40 * std::sin(x * .05) + .2 * y; });
    check(mesh.triangles.size() == 249924, "Mesh size");
    const auto tiling = tileTerrainTriangles(mesh);
    std::vector<Body> bodies;
    bodies.reserve(tiling.tiles.size());
    for (std::size_t t = 0; t < tiling.tiles.size(); ++t) {
        std::vector<Vec3> vertices;
        std::vector<std::array<std::uint32_t, 3>> triangles;
        std::map<std::uint32_t, std::uint32_t> local;
        for (const auto index : tiling.tiles[t]) {
            std::array<std::uint32_t, 3> triangle{};
            for (int corner = 0; corner < 3; ++corner) {
                const auto global = mesh.triangles[index][corner];
                const auto [it, added] = local.try_emplace(global, std::uint32_t(vertices.size()));
                if (added)
                    vertices.push_back(mesh.vertices[global]);
                triangle[corner] = it->second;
            }
            triangles.push_back(triangle);
        }
        bodies.push_back(makeTerrainTile(Id(t + 2), 1, "Tile", vertices, triangles));
    }
    std::vector<const Body *> records;
    for (const auto &body : bodies)
        records.push_back(&body);
    QElapsedTimer timer;
    timer.start();
    const auto dense = terrainContours(records, .01, 0);
    const auto denseMs = timer.elapsed();
    check(dense.tooDense && dense.segments.empty() &&
              dense.estimatedSegments > terrainContourSegmentLimit,
          "A dense interval is refused without generating");
    check(denseMs < 5000, "The refusal is fast (" + std::to_string(denseMs) + " ms)");
    timer.restart();
    const auto contours = terrainContours(records, 1, 0);
    const auto ms = timer.elapsed();
    check(!contours.tooDense && contours.segments.size() <= terrainContourSegmentLimit &&
              contours.segments.size() > 100000,
          "One-metre contours on 250,000 triangles stay within the bound (" +
              std::to_string(contours.segments.size()) + " segments)");
    check(ms < 20000, "Generation takes " + std::to_string(ms) + " ms");
    std::cout << "  250k triangles: refusal " << denseMs << " ms; " << contours.segments.size()
              << " segments in " << ms << " ms ("
              << contours.segments.size() * sizeof(TerrainContourSegment) / 1024 / 1024
              << " MiB)\n";
    // The surface data at the same scale.
    timer.restart();
    const auto surface = terrainSurfaceDisplay(records);
    check(surface.normals.size() == mesh.vertices.size(), "One normal per mesh position");
    std::size_t outer = 0;
    for (const auto &[tile, edges] : surface.outerEdges)
        outer += edges.size();
    check(outer == 2 * (353 + 354), "Only the outer boundary is drawn at 250k triangles");
    std::cout << "  surface display: " << timer.elapsed() << " ms\n";
}

void displayEdit(const QTemporaryDir &files) {
    Document doc;
    const SiteOrigin origin{512300, 5234500, 100, "EPSG:32633", {"hill.dxf", std::string(64, 'c')}};
    setSiteOrigin(doc, origin);
    const auto terrain =
        createTerrain(doc, grid(30, 30, 1, [](double x, double y) { return .1 * x * y / 30; }),
                      record())
            .terrain;
    const auto digest = terrainMeshDigest(doc, terrain);
    const auto revision = doc.revision();
    const auto history = historyTotal(doc);
    // Defaults are omitted from the encoding: a record at its defaults encodes as before.
    const auto plain = encodeTerrainRecord(*doc.bodies().at(terrain)->terrain);
    check(!plain.contains("display"), "Default display settings are not written");
    const auto plainContainer = encodeContainer(doc);
    TerrainDisplay display;
    display.contours = true;
    display.contourInterval = 2.5;
    display.shading = TerrainShading::ElevationBands;
    setTerrainDisplay(doc, terrain, display);
    check(doc.revision() == revision + 1 && historyTotal(doc) == history + 1,
          "One edit, one history entry");
    check(lastLabel(doc) == "Terrain display", "The edit is labelled");
    check(terrainMeshDigest(doc, terrain) == digest &&
              doc.bodies().at(terrain)->terrain->meshDigest == digest &&
              !terrainChangedSinceGenerated(doc, terrain),
          "The mesh digest does not change and the terrain is not changed since generated");
    check(setTerrainDisplay(doc, terrain, display).empty() && doc.revision() == revision + 1,
          "Equal settings change nothing");
    const auto encoded = encodeTerrainRecord(*doc.bodies().at(terrain)->terrain);
    check(encoded["display"].toObject() == QJsonObject{{"contours", true},
                                                        {"contourInterval", 2.5},
                                                        {"shading", "elevation-bands"}},
          "Non-default settings are written");
    TerrainDisplay contoursOnly;
    contoursOnly.contours = true;
    auto only = *doc.bodies().at(terrain)->terrain;
    only.display = contoursOnly;
    check(encodeTerrainRecord(only)["display"].toObject() == QJsonObject{{"contours", true}},
          "Only fields that differ from their defaults are written");
    // Save and reopen.
    const auto path = files.filePath("display.sketchyup");
    saveDocument(doc, path);
    const auto reopened = loadDocument(path);
    check(reopened.bodies().at(terrain)->terrain->display == display,
          "Display settings survive save and reopen");
    check(!terrainChangedSinceGenerated(reopened, terrain), "Reopened terrain is current");
    // Undo restores the defaults and the original encoding, byte for byte.
    doc.undo();
    check(doc.bodies().at(terrain)->terrain->display == TerrainDisplay{} &&
              encodeTerrainRecord(*doc.bodies().at(terrain)->terrain) == plain &&
              documentTree(encodeContainer(doc)) == documentTree(plainContainer),
          "Undo restores the default settings and the exact encoding");
    doc.redo();
    check(doc.bodies().at(terrain)->terrain->display == display, "Redo");
    // The decoder: unknown and empty display objects are refused; out-of-range values too.
    auto withDisplay = [&](QJsonValue value) {
        auto object = plain;
        object["display"] = value;
        return rejection([&] { decodeTerrainRecord(object); });
    };
    check(!withDisplay(QJsonObject{{"labels", true}}).empty(), "Unknown display fields refused");
    check(!withDisplay(QJsonObject{}).empty(), "An empty display object is refused");
    check(!withDisplay(QJsonObject{{"contourInterval", 0}}).empty(), "Interval 0 is refused");
    check(!withDisplay(QJsonObject{{"shading", "slope"}}).empty(), "Unknown shading refused");
    check(withDisplay(QJsonObject{{"shading", "material"}}).empty(),
          "A default value written explicitly is still read");
    // Refusals: a locked terrain, a missing terrain, a bad interval.
    auto locked = std::make_shared<Body>(*doc.bodies().at(terrain));
    locked->locked = true;
    doc.apply({"Lock", {{terrain, doc.bodies().at(terrain), locked}}}, doc.revision());
    TerrainDisplay other;
    check(rejection([&] { setTerrainDisplay(doc, terrain, other); }) ==
              "Cannot edit a locked entity or its contents",
          "A locked terrain refuses display changes");
    check(rejection([&] { setTerrainDisplay(doc, 99999, other); }) == "Terrain body does not exist",
          "A missing terrain is refused");
}

void displayCommand() {
    Document doc;
    const auto terrain =
        createTerrain(doc, grid(6, 6, 1, [](double x, double) { return x; }), record()).terrain;
    const auto digest = terrainMeshDigest(doc, terrain);
    const auto id = QString::number(terrain);
    const auto result = executeBatch(doc, batch(doc, {QJsonObject{{"command", "terrain.set_display"},
                                                                  {"terrain", id},
                                                                  {"contours", true},
                                                                  {"contourInterval", .5}}}));
    const auto operation = result["terrainOperations"].toArray().at(0).toObject();
    check(operation["changed"].toBool() &&
              operation["display"].toObject()["contourInterval"].toDouble() == .5 &&
              operation["display"].toObject()["shading"] == "material",
          "terrain.set_display reports the settings");
    check(doc.bodies().at(terrain)->terrain->display.contours &&
              terrainMeshDigest(doc, terrain) == digest,
          "The command changes the display, not the mesh");
    check(lastLabel(doc) == "Terrain display", "Catalogue label in History");
    // Omitted fields keep their values.
    executeBatch(doc, batch(doc, {QJsonObject{{"command", "terrain.set_display"},
                                              {"terrain", id},
                                              {"shading", "elevationBands"}}}));
    const auto &settings = doc.bodies().at(terrain)->terrain->display;
    check(settings.contours && settings.contourInterval == .5 &&
              settings.shading == TerrainShading::ElevationBands,
          "Omitted fields are unchanged");
    // terrain.describe reports them.
    const auto described = terrainDescription(doc, terrain);
    check(described["display"].toObject()["shading"] == "elevationBands",
          "terrain.describe reports the display settings");
    // Schema-level refusals.
    for (const auto &bad : {QJsonObject{{"contourInterval", 0}}, QJsonObject{{"shading", "slope"}},
                            QJsonObject{{"contours", "yes"}}}) {
        auto command = bad;
        command["command"] = "terrain.set_display";
        command["terrain"] = id;
        const auto revision = doc.revision();
        check(!rejection([&] { executeBatch(doc, batch(doc, {command})); }).empty() &&
                  doc.revision() == revision,
              "Invalid display settings are refused and change nothing");
    }
    // The catalogue entry, hidden from the in-app assistant like every terrain command.
    const auto spec = commandDescription("terrain.set_display");
    check(spec["label"] == "Terrain display" && spec["category"] == "Site and terrain",
          "Catalogue entry");
}

void terrainMaterial() {
    Document doc;
    const auto first =
        createTerrain(doc, grid(4, 4, 1, [](double x, double) { return x; }), record());
    Id material = 0;
    for (const auto &[id, record] : doc.materials())
        if (record->name == "Terrain")
            material = id;
    check(material && doc.materials().size() == 1, "A new terrain adds the Terrain material");
    check(doc.materials().at(material)->color == terrainMaterialColor, "Its colour");
    check(historyTotal(doc) == 1, "Created in the terrain's own edit");
    for (const auto id : first.tiles)
        check(doc.bodies().at(id)->materials == MaterialSides{material, material},
              "Tiles take the material on both sides");
    check(terrainTilePaint(doc, first.terrain).tiles == 0, "The default material is not paint");
    const auto second =
        createTerrain(doc, grid(3, 3, 1, [](double, double y) { return y; }), record());
    check(doc.materials().size() == 1 &&
              doc.bodies().at(second.tiles.front())->materials.front == material,
          "A second terrain reuses the material");
    doc.undo();
    doc.undo();
    check(doc.materials().empty(), "Undo removes the material with the terrain");
    doc.redo();
    // Regenerate keeps the tiles' shared material; painting every tile carries the new one.
    const auto red = createMaterial(doc, "Red earth", {.6f, .3f, .2f});
    for (const auto id : terrainTiles(doc, first.terrain))
        assignMaterial(doc, id, std::nullopt, red);
    check(terrainTilePaint(doc, first.terrain).tiles == 0, "Shared tile material is carried");
    auto rebuilt = grid(4, 4, 1, [](double x, double y) { return x + .1 * y; });
    const auto regenerated = regenerateTerrain(doc, first.terrain, rebuilt,
                                               *doc.bodies().at(first.terrain)->terrain);
    check(regenerated.changed, "Regenerated");
    for (const auto id : regenerated.tiles)
        check(doc.bodies().at(id)->materials.front == red, "New tiles keep the shared material");
}

void surfaceDisplay() {
    Document doc;
    // 60 x 45 cells: 5,400 triangles in several tiles.
    const auto mesh =
        grid(60, 45, .5, [](double x, double y) { return std::sin(x * .3) + std::cos(y * .2); });
    const auto terrain = createTerrain(doc, mesh, record()).terrain;
    const auto records = tiles(doc, terrain);
    check(records.size() > 2, "Several tiles");
    const auto surface = terrainSurfaceDisplay(records);
    std::size_t outer = 0, border = 0;
    for (const auto *tile : records)
        for (const auto &[id, edge] : tile->topology.edges)
            if (!tile->edgeAppearances.contains(id)) {
                ++border;
                outer += surface.outerEdge(tile->id, id);
            } else
                check(!surface.outerEdge(tile->id, id), "Interior edges are not outer edges");
    check(outer == 2 * (60 + 45), "Exactly the terrain's boundary edges are drawn");
    check(border > outer, "Tile seams exist and are not drawn");
    // Normals: one per position, shared by the copies of a seam vertex, equal to the
    // area-weighted average over every triangle at that position (not just one tile's).
    check(surface.normals.size() == mesh.vertices.size(), "Seam copies share one normal");
    std::map<std::tuple<double, double, double>, Vec3> sums;
    for (const auto &t : mesh.triangles) {
        const auto a = mesh.vertices[t[0]], b = mesh.vertices[t[1]], c = mesh.vertices[t[2]];
        const auto n = cross(b - a, c - a);
        for (const auto p : {a, b, c}) {
            auto &sum = sums[{p.x, p.y, p.z}];
            sum = sum + n;
        }
    }
    double worst = 0;
    for (const auto &[key, sum] : sums) {
        const auto expected = sum * (1 / length(sum));
        const auto got = surface.normal({std::get<0>(key), std::get<1>(key), std::get<2>(key)},
                                        {});
        worst = std::max(worst, length(got - expected));
    }
    check(worst < 1e-12, "Normals are continuous across seams (worst " + std::to_string(worst) +
                             ")");
    check(surface.zMin < surface.zMax, "Elevation range");
    const auto low = terrainElevationBandColor(surface.zMin, surface.zMin, surface.zMax),
               high = terrainElevationBandColor(surface.zMax, surface.zMin, surface.zMax);
    check(std::abs(low[0] - 0x4f / 255.f) < 1e-6 && std::abs(high[0] - 0x8c / 255.f) < 1e-6,
          "Bands run from green to brown");
    std::set<std::array<float, 3>> bands;
    for (int i = 0; i <= 100; ++i)
        bands.insert(terrainElevationBandColor(i / 100.0, 0, 1));
    check(bands.size() == 10, "Ten elevation bands");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir files;
    try {
        contoursOnKnownMesh();
        std::cout << "contours on a known mesh\n" << std::flush;
        determinism();
        std::cout << "determinism\n" << std::flush;
        cacheKey();
        std::cout << "cache key\n" << std::flush;
        bound();
        std::cout << "bound at 250,000 triangles\n" << std::flush;
        displayEdit(files);
        std::cout << "display edit\n" << std::flush;
        displayCommand();
        std::cout << "terrain.set_display\n" << std::flush;
        terrainMaterial();
        std::cout << "terrain material\n" << std::flush;
        surfaceDisplay();
        std::cout << "surface display\n" << std::flush;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
