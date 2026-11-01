// D-043: terrain's own history limit class (ADR 0168 decision 3). A terrain up to the
// 250,000-triangle document limit is created, imported, regenerated, re-parameterised and
// exploded as ONE undoable edit, undo and redo restore the exact records, the general history
// budget (192 MiB since 1.7.0, 64 MiB before) is not used by terrain tiles, and the worst tile set
// and record a 250,000-triangle terrain can have still fits one regeneration's before and after.
// Under AddressSanitizer (the serial sanitized CI job) the near-limit core cases run, the
// worst-case accounting is skipped and the command case uses a 120 x 120 point file.
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "core/terrain.hpp"
#include "sanitizer_mode.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
using Bodies = std::map<Id, BodyPtr>;
Bodies bodies(const Document &doc) { return {doc.bodies().begin(), doc.bodies().end()}; }
// Exact: the same immutable records (history shares them with the live document).
bool same(const Document &doc, const Bodies &expected) { return bodies(doc) == expected; }
// The newest entry undoes to `before` and redoes to `after`, revision and site included.
void undoRedo(Document &doc, const Bodies &before, const Bodies &after, const char *what) {
    const auto site = doc.site();
    const auto entries = doc.history().total;
    doc.undo();
    check(same(doc, before), std::string(what) + ": undo restores the records exactly");
    doc.redo();
    check(same(doc, after) && doc.site() == site && doc.history().total == entries,
          std::string(what) + ": redo restores the records exactly");
}
// A gently rolling (nx x ny)-cell grid, two counter-clockwise triangles per cell.
TerrainMesh grid(int nx, int ny, double lift = 0) {
    TerrainMesh mesh;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i)
            mesh.vertices.push_back(
                {double(i), double(j), lift + .25 * std::sin(i * .3) + .15 * std::cos(j * .2)});
    auto at = [&](int i, int j) { return std::uint32_t(j * (nx + 1) + i); };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}
TerrainRecord record(const TerrainMesh &mesh) {
    TerrainRecord result;
    result.generator = "test/terrain-v1";
    result.parameters.maxEdgeLength = 3;
    TerrainSource contour;
    contour.reference = "DXF handle 2F";
    contour.points = {mesh.vertices.front(), mesh.vertices.back()};
    result.sources.push_back(contour);
    return result;
}
std::size_t triangles(const Document &doc) { return doc.materializedTotals().terrainTriangles; }
template <class F> void timed(const char *name, F body) {
    const auto start = std::chrono::steady_clock::now();
    body();
    std::cout << name << ": "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " s\n";
}
// Core: create a 249,218-triangle terrain, replace every tile, undo and redo each.
void createAndRegenerate() {
    Document doc;
    const auto mesh = grid(353, 353);
    const auto created = createTerrain(doc, mesh, record(mesh));
    const auto afterCreate = bodies(doc);
    check(triangles(doc) == 249218 && doc.history().total == 1, "create is one edit");
    // The tiles are charged to terrain's class, not to the general budget. They are more than the
    // 64 MiB general budget that 1.6.1 had, so they could not have fit in it; the general budget
    // is 192 MiB since R110, and the tiles are charged to terrain's class either way.
    check(doc.terrainHistoryBytes() > 64 * 1024 * 1024 && doc.historyBytes() < 64 * 1024,
          "a 250k terrain's tiles are charged to the terrain history class");
    undoRedo(doc, {}, afterCreate, "create");
    // Every tile changes (lifted mesh): the whole tile set before and after in one edit.
    const auto lifted = grid(353, 353, 1);
    const auto regenerated = regenerateTerrain(doc, created.terrain, lifted, record(lifted));
    check(regenerated.changed && doc.history().total == 2 && !doc.history().pruned,
          "regenerate is one edit, and the create entry is kept");
    for (const auto id : created.tiles)
        check(!doc.bodies().contains(id), "every old tile was replaced");
    const auto afterRegenerate = bodies(doc);
    undoRedo(doc, afterCreate, afterRegenerate, "regenerate");
    check(terrainMeshDigest(doc, created.terrain) ==
              doc.bodies().at(created.terrain)->terrain->meshDigest,
          "the regenerated digest matches");
    // A second full regeneration drops the oldest entry (the budget holds the terrain
    // plus one regeneration's before and after), never a refusal.
    const auto again = grid(353, 353, 2);
    regenerateTerrain(doc, created.terrain, again, record(again));
    check(doc.terrainHistoryBytes() <= Document::terrainHistoryLimit && doc.canUndo(),
          "the terrain history stays within its budget");
    doc.verifyIncrementalState();
}
// Core: Explode is the largest terrain edit within the 100,000-face modelling limit
// (99,458 tile faces before and after); a terrain over 100,000 faces refuses.
void explode() {
    Document doc;
    const auto mesh = grid(223, 223);
    const auto terrain = createTerrain(doc, mesh, record(mesh)).terrain;
    const auto before = bodies(doc);
    explodeTerrain(doc, terrain);
    check(!isTerrain(doc, terrain) && doc.history().total == 2, "explode is one edit");
    undoRedo(doc, before, bodies(doc), "explode");
    Document large;
    // 235 x 235 cells is 110,450 faces. Since R110 the unique face limit is 300,000, so this
    // explodes; the refusal one face over that limit is in document_limits_tests.
    const auto big = grid(235, 235);
    const auto bigTerrain = createTerrain(large, big, record(big)).terrain;
    explodeTerrain(large, bigTerrain);
    check(!isTerrain(large, bigTerrain) && large.history().total == 2,
          "explode of a 110k-face terrain is one edit under the 300,000-face limit");
    large.undo();
    check(isTerrain(large, bigTerrain), "undo restores the 110k-face terrain");
}
// The worst a 250,000-triangle terrain can charge: disjoint triangles (three vertices and
// three boundary edges each, the most per triangle) and a record at its source limits.
// One regeneration's before and after of that fits the terrain budget, so no terrain edit
// within the document limit is refused by history.
void worstCase() {
    TerrainMesh mesh;
    for (std::uint32_t i = 0; i < TerrainLimits::triangles; ++i) {
        const double x = (i % 500) * 2.0, y = (i / 500) * 2.0;
        mesh.vertices.push_back({x, y, 0});
        mesh.vertices.push_back({x + 1, y, 0});
        mesh.vertices.push_back({x, y + 1, .5});
        mesh.triangles.push_back({3 * i, 3 * i + 1, 3 * i + 2});
    }
    auto full = record(mesh);
    full.sources.clear();
    for (std::size_t i = 0; i < TerrainLimits::sources; ++i) {
        TerrainSource source;
        source.reference = std::string(TerrainLimits::referenceBytes, 'r');
        source.points.assign(i < TerrainLimits::sourceVertices - 2 * TerrainLimits::sources ? 3 : 2,
                             Vec3{1, 2, 3});
        full.sources.push_back(std::move(source));
    }
    Document doc;
    createTerrain(doc, mesh, full);
    const auto worst = doc.terrainHistoryBytes();
    std::cout << "worst 250k terrain charge: " << worst / 1048576.0 << " MiB\n";
    check(2 * worst + 4096 <= Document::terrainHistoryLimit,
          "one regeneration of the worst 250k terrain fits the terrain history budget");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject run(Document &doc, QJsonObject command) {
    const auto response = executeBatch(doc, batch(doc, {command}), BatchResponse::CreatedIds);
    return response["terrainOperations"].toArray().at(0).toObject();
}
// A point file on a jittered 1 m grid. 354 x 353 points (124,962) give just under 250,000
// triangles with the convex hull.
QByteArray pointFile(int columns, int rows) {
    QByteArray csv;
    std::uint32_t state = 12345;
    auto jitter = [&] {
        state = state * 1664525u + 1013904223u;
        return (double(state >> 8) / double(1u << 24) - .5) * .3;
    };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < columns; ++i)
            csv += QByteArray::number(500 + i + jitter(), 'f', 4) + ' ' +
                   QByteArray::number(800 + j + jitter(), 'f', 4) + ' ' +
                   QByteArray::number(10 + .25 * std::sin(i * .3) + .15 * std::cos(j * .2), 'f',
                                      4) +
                   '\n';
    return csv;
}
// Commands: terrain.import, terrain.set_parameters and terrain.regenerate near 250k.
void commands() {
    const bool full = !test::sanitized;
    Document doc;
    const auto imported =
        run(doc, {{"command", "terrain.import"},
                  {"data", QString::fromLatin1(
                               (full ? pointFile(354, 353) : pointFile(120, 120)).toBase64())},
                  {"format", "points"},
                  {"units", "m"},
                  {"columns", "XYZ"},
                  {"delimiter", "whitespace"},
                  {"header", false}});
    const auto terrain = imported["terrain"].toString().toULongLong();
    std::cout << "imported " << triangles(doc) << " triangles in "
              << imported["tiles"].toArray().size() << " tiles\n";
    check(isTerrain(doc, terrain) && triangles(doc) > (full ? 240000u : 28000u) &&
              triangles(doc) <= TerrainLimits::triangles && doc.history().total == 1 &&
              doc.history(0, 1).entries.at(0).label == "Import terrain" && doc.site(),
          "terrain.import of a near-limit terrain is one edit");
    const auto afterImport = bodies(doc);
    doc.undo();
    check(doc.bodies().empty() && !doc.site(), "undo removes the terrain and the site origin");
    doc.redo();
    check(same(doc, afterImport) && doc.site(), "redo restores the import exactly");
    // Trimming long outer edges changes the bottom-left tile, so every tile takes a new ID.
    const auto oldTiles = terrainTiles(doc, terrain);
    run(doc, {{"command", "terrain.set_parameters"},
              {"terrain", QString::number(terrain)},
              {"boundary", "maxEdgeLength"},
              {"maxEdgeLength", 1.2}});
    std::size_t replaced = 0;
    for (const auto id : oldTiles)
        replaced += !doc.bodies().contains(id);
    std::cout << "set_parameters replaced " << replaced << " of " << oldTiles.size()
              << " tiles; terrain history " << doc.terrainHistoryBytes() / 1048576.0 << " MiB\n";
    check(doc.history().total == 2 && replaced * 10 >= oldTiles.size() * 9 &&
              doc.bodies().at(terrain)->terrain->parameters.boundary ==
                  TerrainBoundaryMode::MaxEdgeLength,
          "terrain.set_parameters replaces the tile set in one edit");
    const auto afterSettings = bodies(doc);
    undoRedo(doc, afterImport, afterSettings, "set_parameters");
    // Regenerate from the stored sources with the same parameters: nothing changes.
    const auto revision = doc.revision();
    try {
        run(doc, {{"command", "terrain.regenerate"}, {"terrain", QString::number(terrain)}});
    } catch (const std::exception &) {
        // "Batch has no committed changes"
    }
    check(doc.revision() == revision && doc.history().total == 2 && same(doc, afterSettings),
          "an unchanged terrain.regenerate makes no edit");
    // Back to the convex hull through settings. When the entries no longer fit the terrain
    // budget the oldest is dropped; the newest entries stay undoable.
    run(doc, {{"command", "terrain.set_parameters"},
              {"terrain", QString::number(terrain)},
              {"boundary", "convexHull"}});
    std::cout << "after a second settings change: " << doc.history().total << " entries, pruned "
              << doc.history().pruned << ", terrain history "
              << doc.terrainHistoryBytes() / 1048576.0 << " MiB\n";
    check(doc.canUndo() && doc.history().position == doc.history().total &&
              doc.terrainHistoryBytes() <= Document::terrainHistoryLimit,
          "the terrain history stays within its budget");
    undoRedo(doc, afterSettings, bodies(doc), "set_parameters back");
    doc.undo();
    check(same(doc, afterSettings), "undo returns to the first settings change");
    if (!full)
        return;
    // Since R110 the modelling face limit is 300,000. This near-250k terrain has no loose faces,
    // so its explode is under the limit: the command goes through, and undo restores the terrain.
    const auto unchanged = doc.revision();
    run(doc, {{"command", "terrain.explode"}, {"body", QString::number(terrain)}});
    check(doc.revision() == unchanged + 1 && !isTerrain(doc, terrain),
          "terrain.explode of a near-limit terrain is one edit under the 300,000-face limit");
    doc.undo();
    check(isTerrain(doc, terrain), "undo restores the near-limit terrain");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        timed("create and regenerate", createAndRegenerate);
        timed("explode", explode);
        if (!test::sanitized)
            timed("worst case", worstCase);
        timed("commands", commands);
        std::cout << "terrain history tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
