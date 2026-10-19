// R095.c2 terrain.create_from_edges, terrain.set_parameters and terrain.regenerate (ADR 0168
// sections 2, 3 and 5): regeneration from stored sources as one labelled edit, the tile-ID
// rule, determinism, undo/redo and save/reopen, parameter validation, the paint notice,
// refusals that change nothing, and terrains built from model edges (chaining, roles,
// boundary and holes, edges in nested groups, tile-edge and lock refusals), and R095.h's roles
// by tag for Create terrain from selection (the mapping to the command, validation, Ignore,
// the one-click rule and Regenerate keeping the boundary and holes).
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/terrain_commands.hpp"
#include "core/groups.hpp"
#include "core/tags.hpp"
#include "core/terrain.hpp"
#include "io/document_io.hpp"
#include "io/terrain_import.hpp"
#include "io/terrain_source.hpp"
#include "terrain_import_fixtures.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
using namespace sketchy;
namespace fixture = sketchy::terrain_fixture;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> InspectionError refusal(F operation) {
    try {
        operation();
    } catch (const InspectionError &error) {
        return error;
    } catch (const std::exception &error) {
        return InspectionError("not an inspection error", error.what());
    }
    return InspectionError("", "no refusal");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject run(Document &doc, QJsonObject command) {
    const auto response = executeBatch(doc, batch(doc, {command}));
    return response["terrainOperations"].toArray().at(0).toObject();
}
QJsonObject regenerate(Id terrain) {
    return {{"command", "terrain.regenerate"}, {"terrain", QString::number(terrain)}};
}
QJsonObject settings(Id terrain, QJsonObject fields) {
    fields["command"] = "terrain.set_parameters";
    fields["terrain"] = QString::number(terrain);
    return fields;
}
QJsonObject kind(const QJsonObject &report, const char *list, const QString &name) {
    for (const auto &value : report["terrain"].toObject()[list].toArray())
        if (value.toObject()["kind"] == name)
            return value.toObject();
    return {};
}
// The model without the revision counter, which counts every step, and the ID allocator's
// high-water mark, which Undo keeps (retired IDs are never reused).
QJsonObject model(const Document &doc) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json.remove("revision");
    json.remove("nextId");
    return json;
}
struct Snapshot {
    std::uint64_t revision;
    std::size_t total, position;
    QByteArray encoded;
};
Snapshot snapshot(const Document &doc) {
    return {doc.revision(), doc.history().total, doc.history().position, encodeDocument(doc)};
}
void unchanged(const Document &doc, const Snapshot &before, const std::string &what) {
    check(doc.revision() == before.revision && doc.history().total == before.total &&
              doc.history().position == before.position && encodeDocument(doc) == before.encoded,
          what + " leaves revision, history and records unchanged");
}
Id importSite(Document &doc) {
    const QJsonObject command{
        {"command", "terrain.import"},
        {"data", QString::fromLatin1(fixture::terrainContoursMetres().toBase64())},
        {"format", "dxf"},
        {"roles", QJsonObject{{"BOUNDARY", "boundary"}, {"POND", "hole"}}}};
    return run(doc, command)["terrain"].toString().toULongLong();
}
constexpr const char *metreDigest =
    "c093a02ec36dc4d8544eefb4fc7d99c2d62f7b9ba568331feedf46e974529584";

// A terrain whose stored mesh is not what its sources give: two hand triangles over a
// 40 x 40 point grid. Regenerating it makes more than one tile.
Id gridTerrain(Document &doc) {
    TerrainRecord record;
    record.generator = terrainGenerator;
    record.parameters.boundary = TerrainBoundaryMode::ConvexHull;
    TerrainSource points;
    points.role = TerrainSourceRole::Points;
    points.reference = "points, code (none)";
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < 40; ++j)
            points.points.push_back({i + 0.31 * std::sin(i * 7.1 + j * 3.3),
                                     j + 0.29 * std::cos(i * 2.7 + j * 5.9),
                                     std::sin(i * 0.3) + 0.1 * j});
    record.sources.push_back(points);
    const TerrainMesh mesh{{{0, 0, 0}, {39, 0, 1}, {39, 39, 2}, {0, 39, 1}},
                           {{0, 1, 2}, {0, 2, 3}}};
    return createTerrain(doc, mesh, record).terrain;
}

void regenerateCurrent() {
    Document doc;
    const auto terrain = importSite(doc);
    check(terrainMeshDigest(doc, terrain) == metreDigest, "the imported fixture's digest");
    const auto &record = *doc.bodies().at(terrain)->terrain;
    // The stored sources and parameters give the identical mesh and digest.
    const auto rebuilt = runTerrainRegenerate(record, record.parameters, 0);
    check(rebuilt.ok, "the fixture regenerates: " + rebuilt.message);
    const auto stored = [&] {
        std::vector<const Body *> tiles;
        for (const auto id : terrainTiles(doc, terrain))
            tiles.push_back(doc.bodies().at(id).get());
        return tiles;
    }();
    // Regenerating a current terrain is not an edit: the core operation changes nothing,
    // and a batch holding only it has no committed changes.
    const auto before = snapshot(doc);
    Document copy = doc;
    const auto result = regenerateTerrain(copy, terrain, rebuilt.mesh, rebuilt.record);
    check(!result.changed && result.changes.empty() && encodeDocument(copy) == before.encoded &&
              copy.history().total == before.total,
          "regenerating a current terrain is a no-op");
    check(result.tiles == terrainTiles(doc, terrain), "the no-op keeps the tile IDs");
    const auto error = refusal([&] { executeBatch(doc, batch(doc, {regenerate(terrain)})); });
    check(std::string(error.what()).find("no committed changes") != std::string::npos,
          std::string("a batch regenerating a current terrain commits nothing: ") + error.what());
    unchanged(doc, before, "a current regenerate");
    check(stored.size() == rebuilt.tiles, "the same tile count");
    check(rebuilt.report["terrain"].toObject()["outcome"] == "regeneratedWithNotices" &&
              rebuilt.report["format"] == "terrain-record",
          "the regenerate report has the import's shape");
}

void regenerateChanged() {
    Document doc;
    const auto terrain = importSite(doc);
    const auto container = *doc.bodies().at(terrain);
    // Change a tile's mesh in an edit that also changes the record (as only terrain
    // operations may), so the stored digest no longer matches: "changed since generated".
    {
        const auto tileId = terrainTiles(doc, terrain).front();
        const auto before = doc.bodies().at(tileId);
        auto tile = std::make_shared<Body>(*before);
        tile->surface.vertices.begin()->second.z += 0.5;
        auto edited = std::make_shared<Body>(*doc.bodies().at(terrain));
        edited->terrain->generator = "edited/terrain-v1";
        doc.apply(
            {"Edit tile", {{terrain, doc.bodies().at(terrain), edited}, {tileId, before, tile}}},
            doc.revision());
    }
    check(terrainChangedSinceGenerated(doc, terrain) &&
              terrainDescription(doc, terrain)["changedSinceGenerated"].toBool(),
          "the edited terrain is changed since generated");
    const auto before = model(doc);
    const auto history = doc.history().total;
    const auto operation = run(doc, regenerate(terrain));
    check(operation["changed"].toBool() && operation["command"] == "terrain.regenerate",
          "the regenerate commits");
    check(!terrainChangedSinceGenerated(doc, terrain) &&
              !terrainDescription(doc, terrain)["changedSinceGenerated"].toBool(),
          "changed since generated is false after regenerate");
    check(terrainMeshDigest(doc, terrain) == metreDigest &&
              doc.bodies().at(terrain)->terrain->meshDigest == metreDigest &&
              doc.bodies().at(terrain)->terrain->generator == terrainGenerator,
          "the regenerated digest is the fixture's and the generator is current");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Regenerate terrain",
          "one labelled edit");
    auto kept = *doc.bodies().at(terrain);
    kept.terrain = container.terrain;
    kept.terrain->meshDigest = metreDigest;
    check(kept == container, "the terrain body keeps ID, name, parent, transform and state");
    // Undo and redo are exact.
    const auto after = model(doc);
    doc.undo();
    check(model(doc) == before, "Undo restores the edited terrain exactly");
    doc.redo();
    check(model(doc) == after, "Redo restores the regenerated terrain exactly");
    // Save and reopen.
    const auto saved = encodeContainer(doc);
    const auto reopened = decodeContainer(saved);
    check(encodeContainer(reopened) == saved &&
              *reopened.bodies().at(terrain)->terrain == *doc.bodies().at(terrain)->terrain &&
              terrainMeshDigest(reopened, terrain) == metreDigest &&
              !terrainChangedSinceGenerated(reopened, terrain),
          "save and reopen after regenerate");
}

void tileIdsAndDeterminism() {
    Document doc;
    const auto terrain = gridTerrain(doc);
    const auto oldTiles = terrainTiles(doc, terrain);
    check(oldTiles.size() == 1, "the hand mesh is one tile");
    const auto next = doc.nextId();
    Document twin = doc;
    const auto operation = run(doc, regenerate(terrain));
    const auto tiles = operation["tiles"].toArray();
    check(tiles.size() > 1, "the point grid makes several tiles");
    // A changed mesh cannot keep a tile body (its entity IDs never go back): every tile takes
    // a new ID in row-major order and the old tile is removed.
    for (qsizetype i = 0; i < tiles.size(); ++i)
        check(tiles[i].toString().toULongLong() == next + Id(i),
              "changed tiles take new IDs in row-major order");
    check(!doc.bodies().contains(oldTiles[0]), "the old tile is removed");
    std::vector<Id> listed;
    for (const auto &value : tiles)
        listed.push_back(value.toString().toULongLong());
    check(terrainTiles(doc, terrain) == listed, "the tiles are the terrain's");
    // Determinism: the same regenerate in a copy gives identical records and IDs, and
    // regenerating again changes nothing.
    run(twin, regenerate(terrain));
    check(model(twin) == model(doc), "two regenerates give identical records");
    Document again = doc;
    const auto &record = *doc.bodies().at(terrain)->terrain;
    const auto rebuilt = runTerrainRegenerate(record, record.parameters, 0);
    check(!regenerateTerrain(again, terrain, rebuilt.mesh, rebuilt.record).changed,
          "regenerating twice is identical");
    // A settings change keeps the IDs of the leading unchanged tiles; the rest are new, in
    // row-major order after them.
    const auto nextAfter = doc.nextId();
    run(doc, settings(terrain, {{"boundary", "maxEdgeLength"}, {"maxEdgeLength", 1.2}}));
    const auto changed = terrainTiles(doc, terrain);
    std::size_t prefix = 0;
    while (prefix < changed.size() && prefix < listed.size() && changed[prefix] == listed[prefix])
        ++prefix;
    for (std::size_t i = prefix; i < changed.size(); ++i)
        check(changed[i] == nextAfter + Id(i - prefix), "the other tiles take new IDs in order");
    check(!terrainChangedSinceGenerated(doc, terrain), "the settings change is current");
    doc.undo();
    check(terrainTiles(doc, terrain) == listed, "Undo restores the tiles");
    doc.undo();
    check(terrainTiles(doc, terrain) == oldTiles, "Undo restores the hand mesh");
}

void parameters() {
    Document doc;
    const auto terrain = importSite(doc);
    auto params = [&] { return doc.bodies().at(terrain)->terrain->parameters; };
    check(params().boundary == TerrainBoundaryMode::Boundary && params().maxEdgeLength == 0,
          "the imported terrain uses its boundary");
    // Dry run.
    const auto before = snapshot(doc);
    const auto preview =
        previewBatch(doc, batch(doc, {settings(terrain, {{"boundary", "convexHull"}})}));
    check(preview["status"] == "preview" && preview["terrainOperations"]
                                                    .toArray()[0]
                                                    .toObject()["terrainReport"]
                                                    .toObject()["terrain"]
                                                    .toObject()["parameters"]
                                                    .toObject()["boundary"] == "convexHull",
          "set_parameters dry run reports the new parameters");
    unchanged(doc, before, "the dry run");
    // Invalid combinations refuse and change nothing.
    struct Invalid {
        QJsonObject fields;
        const char *what;
    };
    for (const auto &invalid : {
             Invalid{{{"boundary", "maxEdgeLength"}}, "max edge length mode without a length"},
             Invalid{{{"maxEdgeLength", 0.0001}}, "a length below 1 mm"},
             Invalid{{{"maxEdgeLength", -1}}, "a negative length"},
             Invalid{{{"boundary", "outline"}}, "an unknown mode"},
             Invalid{{{"steinerFill", "yes"}}, "a non-boolean flag"},
         }) {
        const auto error =
            refusal([&] { executeBatch(doc, batch(doc, {settings(terrain, invalid.fields)})); });
        check(error.code() == "INVALID_REQUEST",
              std::string(invalid.what) + " refuses: " + error.code() + " " + error.what());
        unchanged(doc, before, invalid.what);
    }
    // Each mode change: convex hull, max edge length, back to boundary.
    run(doc, settings(terrain, {{"boundary", "convexHull"}}));
    const auto hull = terrainTriangleCount(doc, terrain);
    check(params().boundary == TerrainBoundaryMode::ConvexHull &&
              doc.history().entries.back().label == "Terrain settings" &&
              terrainMeshDigest(doc, terrain) != metreDigest &&
              !terrainChangedSinceGenerated(doc, terrain),
          "convex hull mode regenerates in one \"Terrain settings\" edit");
    run(doc, settings(terrain, {{"boundary", "maxEdgeLength"}, {"maxEdgeLength", 3}}));
    check(params().boundary == TerrainBoundaryMode::MaxEdgeLength && params().maxEdgeLength == 3 &&
              terrainTriangleCount(doc, terrain) < hull,
          "max edge length mode trims the hull");
    run(doc, settings(terrain, {{"boundary", "boundary"}, {"maxEdgeLength", 0}}));
    check(params().boundary == TerrainBoundaryMode::Boundary &&
              terrainMeshDigest(doc, terrain) == metreDigest,
          "back to boundary mode gives the imported mesh");
    // Flat-triangle options.
    const auto flat = run(doc, settings(terrain, {{"flipFlatTriangles", false}}));
    const auto flatReport = flat["terrainReport"].toObject()["terrain"].toObject();
    check(!params().flipFlatTriangles &&
              flatReport["flatTriangles"].toObject()["flips"].toInt() == 0 &&
              flatReport["flatTriangles"].toObject()["remaining"].toInt() > 0,
          "report-only flat mode skips the flips");
    run(doc, settings(terrain, {{"flipFlatTriangles", true}, {"steinerFill", true}}));
    check(params().steinerFill && params().flipFlatTriangles, "Steiner fill is stored");
    // Boundary mode needs a stored boundary source.
    Document grid;
    const auto points = gridTerrain(grid);
    const auto gridBefore = snapshot(grid);
    const auto error = refusal(
        [&] { executeBatch(grid, batch(grid, {settings(points, {{"boundary", "boundary"}})})); });
    check(error.code() == "INVALID_REQUEST" &&
              std::string(error.what()).find("boundary source") != std::string::npos,
          "boundary mode without a boundary source refuses");
    unchanged(grid, gridBefore, "the boundary refusal");
}

void refusals() {
    // Triangulation refusal: two crossing contours.
    Document doc;
    TerrainRecord record;
    record.generator = terrainGenerator;
    record.parameters.boundary = TerrainBoundaryMode::ConvexHull;
    record.sources = {{TerrainSourceRole::Contour, "handle A1", {{0, 0, 1}, {10, 10, 1}}},
                      {TerrainSourceRole::Contour, "handle A2", {{0, 10, 2}, {10, 0, 2}}}};
    const TerrainMesh mesh{{{0, 0, 1}, {10, 10, 1}, {0, 10, 2}}, {{0, 1, 2}}};
    const auto terrain = createTerrain(doc, mesh, record).terrain;
    const auto before = snapshot(doc);
    const auto error = refusal([&] { executeBatch(doc, batch(doc, {regenerate(terrain)})); });
    const auto report = error.details()["terrainReport"].toObject();
    check(error.code() == "INVALID_REQUEST" &&
              report["terrain"].toObject()["outcome"] == "refused" &&
              !report["terrain"].toObject()["refusals"].toArray().isEmpty(),
          std::string("crossing sources refuse the regenerate with the report: ") + error.what());
    const auto crossing = report["terrain"].toObject()["refusals"].toArray()[0].toObject();
    check(crossing["locations"].toArray()[0].toObject()["reference"].toString().startsWith(
              "handle A"),
          "the refusal names the stored source reference");
    unchanged(doc, before, "a triangulation refusal");
    // Triangle budget: the document's other terrains count.
    Document site;
    const auto imported = importSite(site);
    const auto &stored = *site.bodies().at(imported)->terrain;
    const auto budget = runTerrainRegenerate(stored, stored.parameters, 249999);
    check(!budget.ok && budget.code == "LIMIT_EXCEEDED" &&
              kind(budget.report, "refusals", "triangle-budget")["count"].toInt() == 1,
          "the document terrain budget refuses with the report");
    // A locked terrain refuses.
    setEntityState(site, imported, std::nullopt, true);
    const auto locked = snapshot(site);
    const auto lockedError =
        refusal([&] { executeBatch(site, batch(site, {regenerate(imported)})); });
    check(lockedError.code() == "INVALID_OPERATION", "a locked terrain refuses the regenerate");
    unchanged(site, locked, "the lock refusal");
    // Not a terrain.
    const auto missing = refusal([&] { executeBatch(site, batch(site, {regenerate(999)})); });
    check(missing.code() == "NOT_FOUND", "a missing terrain is NOT_FOUND");
}

void paintNotice() {
    Document doc;
    const auto terrain = importSite(doc);
    doc.paint(terrain, {0.4f, 0.3f, 0.2f}); // terrain-level colour
    {
        const auto tileId = terrainTiles(doc, terrain).front();
        const auto before = doc.bodies().at(tileId);
        auto tile = std::make_shared<Body>(*before);
        auto face = tile->surface.faces.begin();
        tile->faceColors[face->first] = {1, 0, 0};
        tile->faceColors[(++face)->first] = {0, 1, 0};
        doc.apply({"Paint", {{tileId, before, tile}}}, doc.revision());
    }
    const auto tilesBefore = terrainTiles(doc, terrain);
    const auto before = snapshot(doc);
    const auto preview = previewBatch(doc, batch(doc, {regenerate(terrain)}));
    const auto previewed =
        preview["terrainOperations"].toArray()[0].toObject()["terrainReport"].toObject();
    check(kind(previewed, "notices", "face-paint-dropped")["count"].toInt() == 2,
          "the dry run reports the dropped face paint with its count");
    unchanged(doc, before, "the paint dry run");
    const auto operation = run(doc, regenerate(terrain));
    check(operation["changed"].toBool() &&
              kind(operation["terrainReport"].toObject(), "notices", "face-paint-dropped")["count"]
                      .toInt() == 2,
          "the change report carries the paint notice");
    for (const auto id : terrainTiles(doc, terrain))
        check(doc.bodies().at(id)->faceColors.empty(), "the new tiles carry no face paint");
    check(doc.bodies().at(terrain)->color == std::array<float, 3>{0.4f, 0.3f, 0.2f},
          "the terrain-level colour is kept");
    check(terrainMeshDigest(doc, terrain) == metreDigest &&
              terrainTiles(doc, terrain) == tilesBefore,
          "the mesh itself is unchanged, so the tiles keep their IDs");
}

// ---- terrain.create_from_edges --------------------------------------------------------------
// One wire body per polyline; returns its body ID.
Id polyline(Document &doc, const std::vector<Vec3> &points, bool closed) {
    Id body = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
        body = doc.addWire(body, points[i], points[i + 1]);
    if (closed)
        doc.addWire(body, points.back(), points.front());
    return body;
}
std::vector<Vec3> square(double half, double z, double cx = 0, double cy = 0) {
    return {{cx - half, cy - half, z},
            {cx + half, cy - half, z},
            {cx + half, cy + half, z},
            {cx - half, cy + half, z}};
}
QJsonArray edgesOf(const Document &doc, Id body) {
    QJsonArray edges;
    for (const auto &[id, edge] : doc.bodies().at(body)->topology.edges)
        edges.append(QJsonObject{{"body", QString::number(body)}, {"edge", QString::number(id)}});
    return edges;
}
QJsonArray join(std::initializer_list<QJsonArray> lists) {
    QJsonArray all;
    for (const auto &list : lists)
        for (const auto &value : list)
            all.append(value);
    return all;
}
QJsonObject create(QJsonArray edges, QJsonObject extra = {}) {
    extra["command"] = "terrain.create_from_edges";
    extra["edges"] = edges;
    return extra;
}
std::set<std::array<double, 3>> meshVertices(const Document &doc, Id terrain) {
    std::set<std::array<double, 3>> vertices;
    for (const auto id : terrainTiles(doc, terrain))
        for (const auto &[vertex, p] : doc.bodies().at(id)->surface.vertices)
            vertices.insert({p.x, p.y, p.z});
    return vertices;
}

void edgesContoursAndBreaklines() {
    Document doc;
    const auto outer = polyline(doc, square(10, 0), true);
    const auto middle = polyline(doc, square(7, 1), true);
    const auto inner = polyline(doc, square(4, 2), true);
    const auto ridge = polyline(doc, {{-2, 0, 2.5}, {0, 0.5, 2.8}, {2, 0, 2.6}}, false);
    std::map<Id, Body> sources;
    for (const auto id : {outer, middle, inner, ridge})
        sources.emplace(id, *doc.bodies().at(id));
    const auto all = join({edgesOf(doc, outer), edgesOf(doc, middle), edgesOf(doc, inner)});
    const auto history = doc.history().total;
    const auto operation = run(
        doc,
        create(all, {{"roles", QJsonObject{{"contour", edgesOf(doc, ridge)}}}, {"name", "Hill"}}));
    const Id terrain = operation["terrain"].toString().toULongLong();
    check(isTerrain(doc, terrain) && doc.bodies().at(terrain)->name == "Hill" &&
              doc.bodies().at(terrain)->parent == 0,
          "terrain.create_from_edges creates a terrain");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Create terrain",
          "one \"Create terrain\" edit");
    const auto &record = *doc.bodies().at(terrain)->terrain;
    std::size_t contours = 0, breaklines = 0;
    for (const auto &source : record.sources) {
        contours += source.role == TerrainSourceRole::Contour;
        breaklines += source.role == TerrainSourceRole::Breakline;
        check(source.reference.starts_with("body "), "edge sources name a model edge");
    }
    check(contours == 3 && breaklines == 1, "constant Z chains are contours, varying Z breaklines");
    const auto report = operation["terrainReport"].toObject();
    check(kind(report, "notices", "contour-not-constant")["count"].toInt() == 1 &&
              kind(report, "notices", "contour-not-constant")["locations"]
                  .toArray()[0]
                  .toObject()["reference"]
                  .toString()
                  .startsWith("body " + QString::number(ridge) + " edge "),
          "a varying \"contour\" becomes a breakline with a located notice");
    check(report["format"] == "edges" && report["terrain"].toObject()["originSource"] == "model" &&
              report["terrain"].toObject()["outcome"] == "createdWithNotices",
          "the report has the import's shape");
    check(record.provenance.sha256.empty() && record.provenance.units == "m" &&
              record.provenance.metresPerUnit == 1 && !doc.site(),
          "model-frame provenance; no site origin is set");
    for (const auto &[id, body] : sources)
        check(*doc.bodies().at(id) == body, "the source edges stay in the model, untouched");
    // The mesh is in the model frame: every source vertex is a mesh vertex.
    const auto vertices = meshVertices(doc, terrain);
    for (const auto &p : square(4, 2))
        check(vertices.contains({p.x, p.y, p.z}), "source vertices are mesh vertices");
    // Canonical: the edges in another order give the identical mesh.
    Document shuffled = doc;
    shuffled.undo();
    QJsonArray reversed;
    for (auto i = all.size(); i-- > 0;)
        reversed.append(all[i]);
    const auto other =
        run(shuffled, create(reversed, {{"roles", QJsonObject{{"contour", edgesOf(doc, ridge)}}}}));
    check(terrainMeshDigest(shuffled, other["terrain"].toString().toULongLong()) ==
              terrainMeshDigest(doc, terrain),
          "edge order does not change the mesh");
    // Undo removes the terrain only.
    doc.undo();
    check(!isTerrain(doc, terrain) && doc.bodies().size() == sources.size(),
          "Undo removes the terrain");
}

void edgesBoundaryAndHole() {
    Document doc;
    const auto outer = polyline(doc, square(10, 0), true);
    const auto middle = polyline(doc, square(7, 1), true);
    const auto inner = polyline(doc, square(4, 2), true);
    const auto boundary = polyline(doc, square(8.5, 0.5), true);
    const auto hole = polyline(doc, square(1, 2), true);
    const auto operation = run(
        doc,
        create(join({edgesOf(doc, outer), edgesOf(doc, middle), edgesOf(doc, inner)}),
               {{"boundary", edgesOf(doc, boundary)}, {"holes", QJsonArray{edgesOf(doc, hole)}}}));
    const Id terrain = operation["terrain"].toString().toULongLong();
    const auto &record = *doc.bodies().at(terrain)->terrain;
    std::size_t boundaries = 0, holes = 0;
    for (const auto &source : record.sources) {
        boundaries += source.role == TerrainSourceRole::Boundary;
        holes += source.role == TerrainSourceRole::Hole;
    }
    check(boundaries == 1 && holes == 1 &&
              record.parameters.boundary == TerrainBoundaryMode::Boundary,
          "boundary and hole sources from closed edge loops");
    for (const auto &p : meshVertices(doc, terrain)) {
        check(std::abs(p[0]) <= 8.5 + 1e-9 && std::abs(p[1]) <= 8.5 + 1e-9,
              "the mesh stays within the boundary");
    }
    const auto elevation = terrainElevationAt(doc, terrain, {{0, 0}, {5.5, 0}});
    check(!elevation[0].found && elevation[1].found, "the hole is excluded");
    // An open boundary refuses with located edges and changes nothing.
    const auto open = polyline(doc, {{-9, -9, 0}, {9, -9, 0}, {9, 9, 0}, {-9, 9, 0}}, false);
    const auto before = snapshot(doc);
    const auto error = refusal([&] {
        executeBatch(doc, batch(doc, {create(join({edgesOf(doc, middle), edgesOf(doc, inner)}),
                                             {{"boundary", edgesOf(doc, open)}})}));
    });
    const auto report = error.details()["terrainReport"].toObject();
    const auto openPolygon = kind(report, "refusals", "open-polygon");
    check(error.code() == "INVALID_REQUEST" && openPolygon["count"].toInt() == 1 &&
              openPolygon["locations"].toArray()[0].toObject()["reference"].toString().startsWith(
                  "body " + QString::number(open) + " edge "),
          std::string("an open boundary refuses with a located edge: ") + error.what());
    check(std::string(error.what()).starts_with("Terrain creation refused"),
          "the refusal names the operation");
    unchanged(doc, before, "the open-boundary refusal");
}

void edgesInGroupsAndRefusals() {
    Document doc;
    const auto a = polyline(doc, square(5, 0), true);
    const auto b = polyline(doc, square(2, 1), true);
    const auto inner = createGroup(doc, {a, b});
    doc.transform(inner, Transform::translation({100, 20, 5}));
    const auto outer = createGroup(doc, {inner});
    doc.transform(outer, Transform::translation({-50, 0, 1}));
    const auto operation = run(doc, create(join({edgesOf(doc, a), edgesOf(doc, b)})));
    const Id terrain = operation["terrain"].toString().toULongLong();
    const auto vertices = meshVertices(doc, terrain);
    const auto world = doc.worldTransform(a);
    for (const auto &[id, p] : doc.bodies().at(a)->surface.vertices) {
        const auto w = world.point(p);
        check(vertices.contains({w.x, w.y, w.z}),
              "edges inside nested groups are taken in world coordinates");
    }
    check(doc.bodies().at(terrain)->parent == 0 &&
              doc.bodies().at(terrain)->transform == Transform{},
          "the terrain is in the model frame");
    // Edges of a terrain tile refuse with the shared message.
    const auto tile = terrainTiles(doc, terrain).front();
    const auto before = snapshot(doc);
    const auto tileError =
        refusal([&] { executeBatch(doc, batch(doc, {create(edgesOf(doc, tile))})); });
    check(tileError.code() == "UNSUPPORTED_TARGET" &&
              std::string(tileError.what()) == terrainProtectedMessage,
          "edges of a terrain tile refuse");
    unchanged(doc, before, "the tile-edge refusal");
    // Locked bodies refuse (the lock of an ancestor counts).
    setEntityState(doc, outer, std::nullopt, true);
    const auto locked = snapshot(doc);
    const auto lockError = refusal(
        [&] { executeBatch(doc, batch(doc, {create(join({edgesOf(doc, a), edgesOf(doc, b)}))})); });
    check(lockError.code() == "INVALID_OPERATION", "edges of a locked body refuse");
    unchanged(doc, locked, "the lock refusal");
    setEntityState(doc, outer, std::nullopt, false);
    // Unknown edges, duplicates and an empty selection.
    QJsonArray missing{QJsonObject{{"body", QString::number(a)}, {"edge", "999"}}};
    check(refusal([&] { executeBatch(doc, batch(doc, {create(missing)})); }).code() == "NOT_FOUND",
          "a missing edge is NOT_FOUND");
    const auto twice = join({edgesOf(doc, a), edgesOf(doc, a)});
    check(refusal([&] { executeBatch(doc, batch(doc, {create(twice)})); }).code() ==
              "INVALID_REQUEST",
          "an edge given twice refuses");
    check(refusal([&] { executeBatch(doc, batch(doc, {create({})})); }).code() == "INVALID_REQUEST",
          "an empty selection refuses");
    // Too few points: one edge.
    const auto single = polyline(doc, {{0, 0, 0}, {1, 0, 0}}, false);
    const auto few =
        refusal([&] { executeBatch(doc, batch(doc, {create(edgesOf(doc, single))})); });
    check(few.code() == "INVALID_REQUEST" && !few.details()["terrainReport"]
                                                  .toObject()["terrain"]
                                                  .toObject()["refusals"]
                                                  .toArray()
                                                  .isEmpty(),
          "insufficient points refuse with the report");
}

void chaining() {
    // A T junction splits into three polylines; a loop drawn in two bodies joins.
    Document doc;
    const auto stem = polyline(doc, {{0, 0, 1}, {0, 5, 2}}, false);
    const auto bar = polyline(doc, {{-5, 5, 2}, {0, 5, 2}, {5, 5, 2}}, false);
    const auto left = polyline(doc, {{0, -10, 0}, {10, -10, 0}, {10, 10, 0}}, false);
    const auto right =
        polyline(doc, {{10, 10, 0}, {-10, 10, 0}, {-10, -10, 0}, {0, -10, 0}}, false);
    TerrainEdgeSelection selection;
    auto refs = [&](Id body) {
        std::vector<TerrainEdgeRef> list;
        for (const auto &[id, edge] : doc.bodies().at(body)->topology.edges)
            list.push_back({body, id});
        return list;
    };
    for (const auto body : {stem, bar})
        for (const auto &ref : refs(body))
            selection.edges.push_back(ref);
    for (const auto body : {left, right})
        for (const auto &ref : refs(body))
            selection.boundary.push_back(ref);
    const auto set = terrainEdgeSources(doc, selection);
    std::size_t open = 0, closed = 0;
    for (const auto &polyline : set.polylines)
        (polyline.closed ? closed : open) += 1;
    check(open == 3 && closed == 1, "a T junction gives three polylines; a split loop joins");
    for (const auto &polyline : set.polylines)
        if (polyline.closed)
            check(polyline.points.size() == 5 && polyline.ref.body == left,
                  "the loop is named by its smallest edge");
}

// ---- R095.h: roles by tag for Create terrain from selection ---------------------------------
std::vector<TerrainEdgeRef> refsOf(const Document &doc, std::initializer_list<Id> bodies) {
    std::vector<TerrainEdgeRef> refs;
    for (const auto body : bodies)
        for (const auto &[id, edge] : doc.bodies().at(body)->topology.edges)
            refs.push_back({body, id});
    return refs;
}
QJsonObject dryRun(const Document &doc, const QJsonObject &command, TerrainImportResult *out = nullptr) {
    const auto result = runTerrainEdgeRequest(parseTerrainEdgeRequest(doc, command));
    if (out)
        *out = result;
    return result.report;
}
void tagRoles() {
    Document doc;
    const auto outer = polyline(doc, square(10, 0), true);
    const auto middle = polyline(doc, square(7, 1), true);
    const auto inner = polyline(doc, square(4, 2), true);
    const auto boundary = polyline(doc, square(8.5, 0.5), true);
    const auto pond = polyline(doc, square(1, 2, 0, -2.5), true);
    const auto island = polyline(doc, square(0.5, 2, 0, -2.5), true);
    const auto notes = polyline(doc, {{-9.5, 5, 5}, {9.5, 5, 5}}, false);
    const auto ridge = polyline(doc, {{-2, 0, 2.5}, {0, 0.5, 2.8}, {2, 0, 2.6}}, false);
    const auto siteTag = createTag(doc, "Site boundary");
    const auto pondTag = createTag(doc, "Ponds");
    const auto islandTag = createTag(doc, "Islands");
    const auto notesTag = createTag(doc, "Notes");
    const auto ridgeTag = createTag(doc, "Ridge");
    assignTag(doc, boundary, siteTag);
    assignTag(doc, pond, pondTag);
    assignTag(doc, island, islandTag);
    assignTag(doc, notes, notesTag);
    // The ridge sits in a tagged group: an Untagged edge is listed under its group's tag.
    const auto ridgeGroup = createGroup(doc, {ridge});
    assignTag(doc, ridgeGroup, ridgeTag);
    check(terrainEdgeTag(doc, ridge) == ridgeTag && terrainEdgeTag(doc, outer) == 0,
          "an Untagged edge takes its nearest tagged ancestor's tag");
    const auto all = refsOf(doc, {outer, middle, inner, boundary, pond, island, notes, ridge});
    check(terrainSelectionNeedsRoles(doc, all), "closed loops and several tags need the roles step");

    // Grouping: Untagged first, then by tag ID, with the command's chaining per tag.
    const auto tags = terrainEdgesByTag(doc, all);
    check(tags.size() == 6 && tags[0].tag == 0 && tags[1].tag == siteTag &&
              tags[5].tag == ridgeTag,
          "one group per tag, Untagged first");
    check(tags[0].edges.size() == 12 && tags[0].chains == 3 && tags[0].closed == 3 &&
              tags[0].constantZ == 3,
          "the Untagged contours are three closed loops of constant Z");
    check(tags[4].tag == notesTag && tags[4].open() == 1 && tags[5].varyingZ == 1,
          "open chains and varying Z are counted");
    check(terrainTagName(doc, 0) == "Untagged" && terrainTagName(doc, pondTag) == "Ponds",
          "tag names");

    // The role mapping reaches the command's lists.
    std::map<Id, TerrainImportRole> roles{{siteTag, TerrainImportRole::Boundary},
                                          {pondTag, TerrainImportRole::Hole},
                                          {islandTag, TerrainImportRole::Ignore},
                                          {notesTag, TerrainImportRole::Ignore},
                                          {ridgeTag, TerrainImportRole::Contour}};
    check(terrainTagRoleProblems(doc, tags, roles).empty(), "valid roles name no problem");
    const auto command = terrainCreateFromEdgesCommand(terrainTagRoleSelection(tags, roles));
    check(command["command"] == "terrain.create_from_edges" &&
              command["edges"].toArray().size() == 12 &&
              command["roles"].toObject()["contour"].toArray().size() == 2 &&
              !command["roles"].toObject().contains("breakline") &&
              command["boundary"].toArray().size() == 4 && command["holes"].toArray().size() == 1 &&
              command["holes"].toArray()[0].toArray().size() == 4,
          "Auto → edges, Contour → roles.contour, Boundary → boundary, each Hole tag → a holes "
          "list");
    for (const auto *key : {"edges", "boundary"})
        for (const auto &edge : command[key].toArray())
            check(edge.toObject()["body"] != QString::number(notes) &&
                      edge.toObject()["body"] != QString::number(island),
                  "Ignore drops the tag's edges");
    // The dry run and the command agree.
    TerrainImportResult preview;
    dryRun(doc, command, &preview);
    check(preview.ok, "the dry run of valid roles creates: " + preview.message);
    const auto history = doc.history().total;
    const auto operation = run(doc, command);
    const Id terrain = operation["terrain"].toString().toULongLong();
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Create terrain",
          "the roles create one \"Create terrain\" edit");
    check(preview.mesh.triangles.size() ==
              std::size_t(operation["terrainReport"].toObject()["terrain"].toObject()["triangles"]
                              .toDouble()),
          "the dry run's mesh is the command's");
    const auto &record = *doc.bodies().at(terrain)->terrain;
    std::size_t boundaries = 0, holes = 0, breaklines = 0;
    for (const auto &source : record.sources) {
        boundaries += source.role == TerrainSourceRole::Boundary;
        holes += source.role == TerrainSourceRole::Hole;
        breaklines += source.role == TerrainSourceRole::Breakline;
    }
    check(boundaries == 1 && holes == 1 && breaklines == 1 &&
              record.parameters.boundary == TerrainBoundaryMode::Boundary,
          "the boundary and hole tags are stored as boundary and hole sources");
    check(kind(operation["terrainReport"].toObject(), "notices", "contour-not-constant")["count"]
                  .toInt() == 1,
          "a forced contour of varying Z becomes a breakline with the command's notice");
    auto inside = [&](Id t) {
        return terrainElevationAt(doc, t, {{0, -2.5}, {5.5, 0}, {9.2, 0}});
    };
    auto found = inside(terrain);
    check(!found[0].found && found[1].found && !found[2].found,
          "the hole removes the triangles inside it; the boundary those outside it");

    // Regenerate keeps the boundary and the hole.
    const auto digest = terrainMeshDigest(doc, terrain);
    {
        const auto tileId = terrainTiles(doc, terrain).front();
        const auto before = doc.bodies().at(tileId);
        auto tile = std::make_shared<Body>(*before);
        tile->surface.vertices.begin()->second.z += 0.5;
        auto edited = std::make_shared<Body>(*doc.bodies().at(terrain));
        edited->terrain->generator = "edited/terrain-v1";
        doc.apply(
            {"Edit tile", {{terrain, doc.bodies().at(terrain), edited}, {tileId, before, tile}}},
            doc.revision());
    }
    check(terrainChangedSinceGenerated(doc, terrain), "the edited terrain is changed");
    check(run(doc, regenerate(terrain))["changed"].toBool(), "Regenerate commits");
    const auto &regenerated = *doc.bodies().at(terrain)->terrain;
    std::size_t keptBoundaries = 0, keptHoles = 0;
    for (const auto &source : regenerated.sources) {
        keptBoundaries += source.role == TerrainSourceRole::Boundary;
        keptHoles += source.role == TerrainSourceRole::Hole;
    }
    found = inside(terrain);
    check(keptBoundaries == 1 && keptHoles == 1 &&
              regenerated.parameters.boundary == TerrainBoundaryMode::Boundary &&
              terrainMeshDigest(doc, terrain) == digest && !found[0].found && found[1].found &&
              !found[2].found,
          "Regenerate keeps the boundary and hole: the stored record rebuilds the same mesh");
    doc.undo();
    doc.undo();
    doc.undo();
    check(!isTerrain(doc, terrain), "Undo removes the terrain");

    // Validation: what the roles step names, and the command refusing the same.
    auto problems = [&](std::map<Id, TerrainImportRole> chosen) {
        return terrainTagRoleProblems(doc, tags, chosen);
    };
    auto refusedWith = [&](std::map<Id, TerrainImportRole> chosen, const char *refusal) {
        TerrainImportResult result;
        const auto report = dryRun(
            doc, terrainCreateFromEdgesCommand(terrainTagRoleSelection(tags, chosen)), &result);
        return !result.ok && !kind(report, "refusals", refusal).isEmpty();
    };
    auto withIgnored = [&](std::map<Id, TerrainImportRole> chosen) {
        chosen.emplace(notesTag, TerrainImportRole::Ignore);
        chosen.emplace(islandTag, TerrainImportRole::Ignore);
        return chosen;
    };
    auto open = withIgnored({{siteTag, TerrainImportRole::Boundary}});
    open[notesTag] = TerrainImportRole::Boundary;
    open[siteTag] = TerrainImportRole::Ignore;
    auto named = problems(open);
    check(named.size() == 1 &&
              named[0] == "Boundary “Notes” has 1 open chain; a boundary is one closed loop.",
          "an open boundary names the tag and the count: " + (named.empty() ? "" : named[0]));
    check(refusedWith(open, "open-polygon"), "the command refuses the open boundary too");
    named = problems(withIgnored({{0, TerrainImportRole::Boundary}}));
    check(named.size() == 1 &&
              named[0] == "Boundary “Untagged” has 3 closed loops; a boundary is one closed loop.",
          "a boundary of several loops is named");
    check(refusedWith(withIgnored({{0, TerrainImportRole::Boundary}}), "multiple-boundaries"),
          "the command refuses several boundary loops too");
    named = problems(withIgnored(
        {{siteTag, TerrainImportRole::Boundary}, {pondTag, TerrainImportRole::Boundary}}));
    check(!named.empty() && named[0] == "Only one tag can be the boundary; “Site boundary” and "
                                        "“Ponds” both have that role.",
          "two boundary tags are named");
    auto hole = withIgnored({});
    hole[notesTag] = TerrainImportRole::Hole;
    named = problems(hole);
    check(named.size() == 1 && named[0] == "Hole “Notes” has 1 open chain; holes are closed loops.",
          "an open hole names the tag and the count");
    std::map<Id, TerrainImportRole> none;
    for (const auto &tag : tags)
        none[tag.tag] = TerrainImportRole::Ignore;
    named = problems(none);
    check(named.size() == 1 && named[0] == "Every tag is ignored; give at least one tag a role.",
          "ignoring every tag is named");
    // Nested holes pass the step's own checks and are refused by the command's dry run.
    auto nested = roles;
    nested[islandTag] = TerrainImportRole::Hole;
    check(problems(nested).empty() && refusedWith(nested, "invalid-polygon"),
          "a hole inside another hole is refused by the command, as before");
    // Auto on the notes line makes it a contour crossing the others: the command refuses.
    auto crossing = roles;
    crossing.erase(notesTag);
    TerrainImportResult crossed;
    dryRun(doc, terrainCreateFromEdgesCommand(terrainTagRoleSelection(tags, crossing)), &crossed);
    check(!crossed.ok, "without Ignore the crossing line refuses");

    // The one-click path: one tag and no closed loop needs no roles step.
    check(!terrainSelectionNeedsRoles(doc, refsOf(doc, {notes})) &&
              !terrainSelectionNeedsRoles(doc, refsOf(doc, {ridge})),
          "one tag and no closed loop: no roles step");
    check(terrainSelectionNeedsRoles(doc, refsOf(doc, {outer})),
          "a closed loop in one tag: the roles step");
    check(terrainSelectionNeedsRoles(doc, refsOf(doc, {notes, ridge})),
          "two tags without loops: the roles step");
    // Auto everywhere is exactly the one-click command's `edges`.
    const auto automatic =
        terrainCreateFromEdgesCommand(terrainTagRoleSelection(terrainEdgesByTag(doc, refsOf(doc, {ridge})), {}));
    check(automatic.size() == 2 && automatic["edges"].toArray().size() == 2,
          "Automatic roles give the one-click command");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        regenerateCurrent();
        regenerateChanged();
        tileIdsAndDeterminism();
        parameters();
        refusals();
        paintNotice();
        edgesContoursAndBreaklines();
        edgesBoundaryAndHole();
        edgesInGroupsAndRefusals();
        chaining();
        tagRoles();
        std::cout << "terrain regenerate tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
