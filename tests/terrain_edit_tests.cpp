// R096.a edit core (ADR 0169 §2, §3, §7): the terrain-v1 amendments (source ids, stored tiling,
// tile keys, the edit list, the key-order digest), structural sharing in the history charge,
// the edit mesh and its grid index (terrainElevationAt), stable tiling and local tile
// replacement, the generalised tile-ID rule and the seam checker. The test-only kinds of
// terrain_edit_test_kinds.hpp stand in for R096.b–e's operations.
#include "core/component_parameter_records.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "io/document_io.hpp"
#include "io/terrain_io.hpp"
#include "sanitizer_mode.hpp"
#include "terrain_edit_test_kinds.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <limits>
#include <random>
#include <set>
using namespace sketchy;
namespace kinds = terrain_test_kinds;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejection(F operation, std::string *code = nullptr) {
    try {
        operation();
    } catch (const TerrainError &error) {
        if (code)
            *code = error.code();
        return error.what();
    } catch (const std::exception &error) {
        if (code)
            *code = {};
        return error.what();
    }
    return {};
}
// A rolling (nx x ny)-cell grid, two counter-clockwise triangles per cell.
TerrainMesh grid(int nx, int ny, double spacing = 1, Vec3 origin = {}) {
    TerrainMesh mesh;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i)
            mesh.vertices.push_back({origin.x + i * spacing, origin.y + j * spacing,
                                     origin.z + .25 * std::sin(i * .3) + .15 * std::cos(j * .2)});
    auto at = [&](int i, int j) { return std::uint32_t(j * (nx + 1) + i); };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}
TerrainRecord record(const TerrainMesh &mesh, std::size_t points = 1) {
    TerrainRecord result;
    result.generator = "test/terrain-v1";
    result.parameters.maxEdgeLength = 3;
    TerrainSources::Vector sources;
    sources.push_back({TerrainSourceRole::Contour, "DXF handle 2F",
                       {mesh.vertices.front(), mesh.vertices.back()}});
    TerrainSource cloud{TerrainSourceRole::Points, "CSV line 4", {}};
    for (std::size_t i = 0; i < points; ++i)
        cloud.points.push_back(mesh.vertices[i % mesh.vertices.size()]);
    sources.push_back(cloud);
    result.sources = std::move(sources);
    return result;
}
// The model without the revision counter (it counts every step) and, with `allocator`, the ID
// allocator's high-water mark, which Undo keeps (retired IDs are never reused).
QByteArray model(const Document &doc, bool allocator = true) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json.remove("revision");
    if (!allocator)
        json.remove("nextId");
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}
std::vector<BodyPtr> tilesOf(const Document &doc, Id terrain) {
    std::vector<BodyPtr> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(doc.bodies().at(id));
    return tiles;
}
// The R095 digest: tiles in ascending ID order.
std::string idOrderDigest(const Document &doc, Id terrain) {
    std::string bytes;
    const auto ids = terrainTiles(doc, terrain);
    const auto n = std::uint32_t(ids.size());
    for (int i = 0; i < 4; ++i)
        bytes.push_back(char((n >> (8 * i)) & 0xFF));
    for (const auto id : ids)
        appendTerrainTileBytes(bytes, *doc.bodies().at(id));
    return digestHex(sha256Digest(bytes));
}
// R095's full scan for terrainElevationAt (the reference for the indexed path).
std::vector<TerrainElevation> scanElevation(const Document &doc, Id terrain,
                                            const std::vector<std::array<double, 2>> &points) {
    struct Tile {
        Id id{};
        const Body *body{};
        std::map<Id, Vec3> world;
        double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    };
    std::vector<Tile> tiles;
    for (const auto id : terrainTiles(doc, terrain)) {
        Tile t{id, doc.bodies().at(id).get(), {}};
        const auto frame = doc.worldTransform(id);
        for (const auto &[v, p] : t.body->surface.vertices) {
            const auto w = frame.point(p);
            t.world[v] = w;
            t.minX = std::min(t.minX, w.x);
            t.maxX = std::max(t.maxX, w.x);
            t.minY = std::min(t.minY, w.y);
            t.maxY = std::max(t.maxY, w.y);
        }
        tiles.push_back(std::move(t));
    }
    std::vector<TerrainElevation> result;
    for (const auto &[x, y] : points) {
        TerrainElevation best;
        for (const auto &t : tiles) {
            const auto id = t.id;
            const auto &tile = *t.body;
            const auto &world = t.world;
            if (x < t.minX - 1e-9 || x > t.maxX + 1e-9 || y < t.minY - 1e-9 || y > t.maxY + 1e-9)
                continue;
            for (const auto &[face, record] : tile.surface.faces) {
                const auto &loop = record.loops[0];
                const auto a = world.at(loop[0]), b = world.at(loop[1]), c = world.at(loop[2]);
                const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                if (std::abs(area) < 1e-18)
                    continue;
                const double u = ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) / area,
                             v = ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) / area,
                             w = 1 - u - v;
                auto beyond = [&](double weight, Vec3 p, Vec3 q) {
                    return weight < 0 &&
                           -weight * std::abs(area) > tolerance * std::hypot(q.x - p.x, q.y - p.y);
                };
                if (beyond(u, b, c) || beyond(v, c, a) || beyond(w, a, b))
                    continue;
                const double z = u * a.z + v * b.z + w * c.z;
                if (!best.found || z > best.elevation)
                    best = {true, z, id, face};
            }
        }
        result.push_back(best);
    }
    return result;
}
bool sameElevations(const std::vector<TerrainElevation> &a, const std::vector<TerrainElevation> &b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].found != b[i].found || a[i].tile != b[i].tile || a[i].face != b[i].face ||
            std::bit_cast<std::uint64_t>(a[i].elevation) != std::bit_cast<std::uint64_t>(b[i].elevation))
            return false;
    return true;
}
std::vector<std::array<double, 2>> randomPoints(std::size_t count, double low, double high,
                                                unsigned seed) {
    std::mt19937_64 random(seed);
    std::uniform_real_distribution<double> d(low, high);
    std::vector<std::array<double, 2>> points;
    for (std::size_t i = 0; i < count; ++i)
        points.push_back({d(random), d(random)});
    return points;
}

void stableTilingAndDigest() {
    Document doc;
    const auto mesh = grid(120, 90);
    const auto created = createTerrain(doc, mesh, record(mesh));
    const auto &body = *doc.bodies().at(created.terrain);
    const auto tiling = tileTerrainTriangles(mesh);
    check(body.terrain->tiling == TerrainTileGrid{tiling.originX, tiling.originY, tiling.cell} &&
              body.terrain->tiling.cell > 0,
          "the record stores the creation grid");
    check(body.terrain->sources[0].id == 1 && body.terrain->sources[1].id == 2,
          "sources are numbered 1..n in canonical order");
    TerrainTileKey previous{INT64_MIN, 0, {}, 0};
    std::size_t index = 0;
    for (const auto id : created.tiles) {
        const auto &tile = *doc.bodies().at(id);
        check(tile.tileKey == tiling.keys.at(index++), "each tile has its creation key");
        check(previous < tile.tileKey, "key order is ID order for a terrain never edited");
        previous = tile.tileKey;
    }
    check(terrainMeshDigest(doc, created.terrain) == idOrderDigest(doc, created.terrain) &&
              body.terrain->meshDigest == idOrderDigest(doc, created.terrain),
          "the key-order digest equals the R095 ID-order digest");
    check(body.terrain->generator == "test/terrain-v1", "a caller's generator is kept");
    check(std::string(terrainEditGenerator) == "cdt-2.0.0/terrain-v1+edit-v1",
          "the generator names the edit part");
    std::cout << "stable tiling: " << created.tiles.size() << " tiles\n";
}

void formatRoundTrip() {
    Document doc;
    const auto mesh = grid(80, 60, 1.5, {10, -20, 3});
    const auto created = createTerrain(doc, mesh, record(mesh, 5));
    const auto key = doc.bodies().at(created.tiles.at(3))->tileKey;
    applyTerrainEdit(doc, created.terrain, "test.lift", kinds::lift(key, .75), "Lift");
    applyTerrainEdit(doc, created.terrain, "test.split", kinds::split(40.2, 15.3), "Split");
    const auto encoded = encodeDocument(doc);
    const auto reopened = decodeDocument(encoded);
    check(encodeDocument(reopened) == encoded, "the edit list, keys and tiling round-trip bit for bit");
    check(*reopened.bodies().at(created.terrain)->terrain == *doc.bodies().at(created.terrain)->terrain,
          "the record round-trips");
    for (const auto &[id, tile] : doc.bodies())
        if (tile->terrainTile)
            check(reopened.bodies().at(id)->tileKey == tile->tileKey, "tile keys round-trip");
    const auto container = encodeContainer(doc);
    check(encodeContainer(decodeContainer(container)) == container, "the container round-trips");
    // The JSON shape (ADR 0108 schema 29, R096.a addendum).
    const auto tree = QJsonDocument::fromJson(encoded).object();
    QJsonObject terrainJson, tileJson;
    for (const auto &value : tree["bodies"].toArray()) {
        const auto o = value.toObject();
        if (o.contains("terrain"))
            terrainJson = o["terrain"].toObject();
        if (o.contains("terrainTile") && tileJson.isEmpty())
            tileJson = o["terrainTile"].toObject();
    }
    check(terrainJson["sources"].toArray()[0].toObject()["id"] == 1, "sources carry ids");
    check(terrainJson["tiling"].toObject()["origin"].toArray().size() == 2 &&
              terrainJson["tiling"].toObject()["cell"].toDouble() > 0,
          "the record carries its tiling");
    const auto edits = terrainJson["edits"].toArray();
    check(edits.size() == 2 && edits[0].toObject()["id"] == 1 &&
              edits[0].toObject()["kind"] == "test.lift" && edits[1].toObject()["id"] == 2,
          "the edit list is written in order with ids and kinds");
    check(tileJson["key"].toArray().size() == 4, "a tile marker is {key: [row, column, path, part]}");
    check(terrainJson["generator"] == "cdt-2.0.0/terrain-v1+edit-v1", "an edit sets the generator");

    // Refusals: each a changed copy of the valid tree.
    auto mutate = [&](const std::function<void(QJsonObject &terrain, QJsonObject &tile)> &change) {
        auto root = tree;
        auto bodies = root["bodies"].toArray();
        bool tileDone = false;
        for (qsizetype i = 0; i < bodies.size(); ++i) {
            auto o = bodies[i].toObject();
            QJsonObject unused;
            if (o.contains("terrain")) {
                auto t = o["terrain"].toObject();
                change(t, unused);
                o["terrain"] = t;
            }
            if (o.contains("terrainTile") && !tileDone) {
                auto marker = o["terrainTile"].toObject();
                QJsonObject none;
                change(none, marker);
                o["terrainTile"] = marker;
                tileDone = true;
            }
            bodies[i] = o;
        }
        root["bodies"] = bodies;
        return rejection([&] { decodeDocument(QJsonDocument(root).toJson(QJsonDocument::Compact)); });
    };
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (t.isEmpty())
                  return;
              auto e = t["edits"].toArray();
              auto first = e[1].toObject();
              first["id"] = 1;
              e[1] = first;
              t["edits"] = e;
          }).empty(),
          "duplicate edit ids are refused");
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (t.isEmpty())
                  return;
              auto e = t["edits"].toArray();
              auto first = e[0].toObject();
              first["kind"] = "sculpt";
              e[0] = first;
              t["edits"] = e;
          }).empty(),
          "an unknown edit kind is refused");
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (!t.isEmpty())
                  t["edits"] = QJsonArray{};
          }).empty(),
          "an empty edit list is written as no list");
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (t.isEmpty())
                  return;
              QJsonArray e;
              const auto entry = t["edits"].toArray()[0].toObject();
              for (int i = 0; i < 4097; ++i) {
                  auto copy = entry;
                  copy["id"] = i + 1;
                  e.append(copy);
              }
              t["edits"] = e;
          }).empty(),
          "more than 4,096 edits are refused");
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (t.isEmpty())
                  return;
              auto s = t["sources"].toArray();
              auto second = s[1].toObject();
              second["id"] = 1;
              s[1] = second;
              t["sources"] = s;
          }).empty(),
          "duplicate source ids are refused");
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (!t.isEmpty())
                  t["tiling"] = QJsonObject{{"origin", QJsonArray{0, 0}}, {"cell", 0}};
          }).empty(),
          "a zero cell is refused");
    check(!mutate([](QJsonObject &, QJsonObject &m) {
              if (!m.isEmpty())
                  m["key"] = QJsonArray{0, 0, "4", 0};
          }).empty(),
          "a quadrant digit beyond 3 is refused");
    check(!mutate([](QJsonObject &, QJsonObject &m) {
              if (!m.isEmpty())
                  m["key"] = QJsonArray{0.5, 0, "", 0};
          }).empty(),
          "a fractional row is refused");
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (!t.isEmpty())
                  t.remove("tiling");
          }).empty(),
          "a record without tiling but keyed tiles mixes the forms and is refused");
    {
        // Two tiles of one terrain with the same key: refused at load, naming both.
        auto root = tree;
        auto bodies = root["bodies"].toArray();
        QJsonValue firstKey;
        bool found = false;
        for (qsizetype i = 0; i < bodies.size(); ++i) {
            auto o = bodies[i].toObject();
            if (!o.contains("terrainTile"))
                continue;
            if (!found) {
                firstKey = o["terrainTile"];
                found = true;
                continue;
            }
            o["terrainTile"] = firstKey;
            bodies[i] = o;
            break;
        }
        root["bodies"] = bodies;
        const auto message = rejection(
            [&] { decodeDocument(QJsonDocument(root).toJson(QJsonDocument::Compact)); });
        check(message.find("two tiles with the key") != std::string::npos,
              "duplicate tile keys are refused at load: " + message);
    }
    check(!mutate([](QJsonObject &t, QJsonObject &) {
              if (!t.isEmpty())
                  t["nextEdit"] = 2;
          }).empty(),
          "a next edit ID not above every entry's is refused");
    check(terrainJson["nextEdit"] == 3, "the record stores the next edit ID");
    std::cout << "format round trip: " << encoded.size() << " bytes\n";
}

void legacyForm() {
    // A document as an R095 build wrote it: no source ids, no tiling, `terrainTile: true`.
    Document doc;
    const auto mesh = grid(100, 70);
    createTerrain(doc, mesh, record(mesh, 3));
    const auto current = encodeDocument(doc);
    auto root = QJsonDocument::fromJson(current).object();
    auto bodies = root["bodies"].toArray();
    for (qsizetype i = 0; i < bodies.size(); ++i) {
        auto o = bodies[i].toObject();
        if (o.contains("terrain")) {
            auto t = o["terrain"].toObject();
            t.remove("tiling");
            auto sources = t["sources"].toArray();
            for (qsizetype s = 0; s < sources.size(); ++s) {
                auto source = sources[s].toObject();
                source.remove("id");
                sources[s] = source;
            }
            t["sources"] = sources;
            o["terrain"] = t;
        }
        if (o.contains("terrainTile"))
            o["terrainTile"] = true;
        bodies[i] = o;
    }
    root["bodies"] = bodies;
    const auto legacy = decodeDocument(QJsonDocument(root).toJson(QJsonDocument::Compact));
    check(encodeDocument(legacy) == current,
          "an R095 document loads with the derived ids, tiling and keys, and saves in the new form");
    std::cout << "legacy form: loads and re-saves\n";
}

void localReplacement() {
    Document doc;
    const auto mesh = grid(200, 150);
    const auto created = createTerrain(doc, mesh, record(mesh));
    const auto terrain = created.terrain;
    check(created.tiles.size() > 20, "the grid has many tiles");
    const auto before = tilesOf(doc, terrain);
    const auto target = before.at(before.size() / 2);
    const auto probes = randomPoints(2000, 0, 200, 7);
    const auto elevationsBefore = scanElevation(doc, terrain, probes);
    check(sameElevations(terrainElevationAt(doc, terrain, std::vector<std::array<double, 2>>(
                                                              probes.begin(), probes.begin() + 200)),
                         std::vector<TerrainElevation>(elevationsBefore.begin(),
                                                       elevationsBefore.begin() + 200)),
          "the indexed elevation query equals R095's scan bit for bit");
    const auto revision = doc.revision();
    const auto applied =
        applyTerrainEdit(doc, terrain, "test.lift", kinds::lift(target->tileKey, 2.5), "Lift tile");
    check(doc.revision() == revision + 1 && doc.history().entries.back().label == "Lift tile",
          "one labelled edit");
    check(applied.tiles.removed == std::vector<Id>{target->id} && applied.tiles.added.size() == 1,
          "exactly the one tile is rebuilt");
    const auto after = tilesOf(doc, terrain);
    std::size_t untouched = 0;
    for (const auto &tile : before)
        if (tile != target) {
            check(doc.bodies().at(tile->id) == tile,
                  "every other tile is pointer-identical with its ID");
            ++untouched;
        }
    check(untouched == before.size() - 1 && after.size() == before.size(),
          "the tile count is unchanged");
    const auto &rebuilt = *doc.bodies().at(applied.tiles.added.front());
    check(rebuilt.tileKey == target->tileKey && rebuilt.name == target->name &&
              rebuilt.materials == target->materials,
          "the rebuilt tile keeps its key, name and tile-level material");
    check(rebuilt.id > target->id, "the rebuilt tile takes a new ID");
    // Border vertices are shared with neighbours and do not move; interior ones rise.
    const auto oldMesh = terrainTileMesh(*target), newMesh = terrainTileMesh(rebuilt);
    std::size_t moved = 0;
    for (std::size_t i = 0; i < oldMesh.vertices.size(); ++i) {
        check(oldMesh.vertices[i].x == newMesh.vertices[i].x &&
                  oldMesh.vertices[i].y == newMesh.vertices[i].y,
              "plan positions are unchanged");
        if (newMesh.vertices[i].z != oldMesh.vertices[i].z) {
            check(newMesh.vertices[i].z == oldMesh.vertices[i].z + 2.5, "lifted by the amount");
            ++moved;
        }
    }
    check(moved > 0 && moved < oldMesh.vertices.size(), "interior vertices move, border ones do not");
    const auto record = *doc.bodies().at(terrain)->terrain;
    check(record.edits.size() == 1 && record.edits[0]->id == 1 &&
              record.generator == terrainEditGenerator && !terrainChangedSinceGenerated(doc, terrain),
          "the record lists the edit and its digest is current");
    // Elevations outside the tile's box are bit-identical.
    const auto elevationsAfter = scanElevation(doc, terrain, probes);
    double minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
    for (const auto &p : oldMesh.vertices) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    std::size_t outside = 0;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const auto [x, y] = probes[i];
        if (x >= minX - 1e-6 && x <= maxX + 1e-6 && y >= minY - 1e-6 && y <= maxY + 1e-6)
            continue;
        ++outside;
        check(std::bit_cast<std::uint64_t>(elevationsAfter[i].elevation) ==
                      std::bit_cast<std::uint64_t>(elevationsBefore[i].elevation) &&
                  elevationsAfter[i].tile == elevationsBefore[i].tile,
              "elevation outside the region is bit-equal");
    }
    check(outside > 1500, "most probes are outside the tile");
    for (std::size_t i = 0; i < probes.size(); i += 200) {
        const std::vector<std::array<double, 2>> chunk(probes.begin() + std::ptrdiff_t(i),
                                                       probes.begin() + std::ptrdiff_t(i + 200));
        check(sameElevations(terrainElevationAt(doc, terrain, chunk),
                             std::vector<TerrainElevation>(elevationsAfter.begin() + std::ptrdiff_t(i),
                                                           elevationsAfter.begin() +
                                                               std::ptrdiff_t(i + 200))),
              "the indexed query follows the edit (cache updated)");
    }
    check(checkTerrainSeams(doc, terrain).ok(), "seams hold after the lift");
    // Undo restores the tiles pointer-identical and the encoding bit for bit; redo the edit.
    const auto encodedAfter = model(doc);
    doc.undo();
    check(tilesOf(doc, terrain) == before, "undo restores the tiles pointer-identical");
    check(doc.bodies().at(terrain)->terrain->edits.empty(), "undo restores the record");
    doc.redo();
    check(model(doc) == encodedAfter, "redo restores the edit bit for bit");
    // Determinism: the same edit on a copy gives identical records and IDs.
    Document other;
    const auto again = createTerrain(other, mesh, ::record(mesh));
    applyTerrainEdit(other, again.terrain, "test.lift", kinds::lift(target->tileKey, 2.5), "Lift tile");
    check(*other.bodies().at(again.terrain)->terrain == record &&
              terrainTiles(other, again.terrain) == terrainTiles(doc, terrain),
          "the same edit on another document gives identical records and IDs");
    std::cout << "local replacement: " << moved << " vertices moved, " << untouched
              << " tiles untouched\n";
}

void topologyAndSplit() {
    // A small cell structure: repeated splits in one cell overflow it into quadrants.
    Document doc;
    const auto mesh = grid(60, 60);
    const auto created = createTerrain(doc, mesh, record(mesh));
    const auto terrain = created.terrain;
    const auto cellBefore = tilesOf(doc, terrain);
    std::mt19937_64 random(11);
    std::uniform_real_distribution<double> d(0.05, 0.95);
    std::size_t splits = 0;
    const auto grid0 = doc.bodies().at(terrain)->terrain->tiling;
    // Split inside one cell many times.
    const auto target = cellBefore.front()->tileKey;
    double cx = grid0.originX + (double(target.column) + .5) * grid0.cell,
           cy = grid0.originY + (double(target.row) + .5) * grid0.cell;
    while (splits < 40) {
        const double x = cx + (d(random) - .5) * grid0.cell * .9,
                     y = cy + (d(random) - .5) * grid0.cell * .9;
        if (rejection([&] {
                applyTerrainEdit(doc, terrain, "test.split", kinds::split(x, y), "Split");
            }).empty())
            ++splits;
    }
    check(doc.history().entries.size() == 1 + splits, "each split is one edit");
    // One refinement adds 3^7 - 1 triangles in the cell: it overflows into quadrants.
    applyTerrainEdit(doc, terrain, "test.refine", kinds::refine(cx + .1, cy + .2, 7), "Refine");
    std::set<std::string> paths;
    std::size_t triangles = 0;
    for (const auto &tile : tilesOf(doc, terrain)) {
        check(tile->surface.faces.size() <= TerrainLimits::tileTriangles, "tiles stay within 2,048");
        if (tile->tileKey.row == target.row && tile->tileKey.column == target.column)
            paths.insert(tile->tileKey.path);
        triangles += tile->surface.faces.size();
    }
    check(paths.size() > 1 && !paths.contains(""), "the overflowing cell split into quadrants");
    check(triangles == mesh.triangles.size() + 2 * splits + 2186,
          "every split adds two triangles and the refinement 2,186");
    const auto seams = checkTerrainSeams(doc, terrain);
    check(seams.ok(), "seams hold after splits: " +
                          (seams.problems.empty() ? std::string() : seams.problems.front()));
    // Cells far from the splits are untouched.
    for (const auto &tile : cellBefore)
        if (std::abs(tile->tileKey.row - target.row) > 1 ||
            std::abs(tile->tileKey.column - target.column) > 1)
            check(doc.bodies().contains(tile->id) && doc.bodies().at(tile->id) == tile,
                  "distant tiles are pointer-identical");
    // A split into a quadrant adds there; the structure stays a partition by centroid.
    const auto em = terrainEditMesh(doc, terrain);
    for (const auto *tile : em.tiles())
        for (const auto &t : tile->content->triangles) {
            const auto a = tile->content->vertices[t[0]], b = tile->content->vertices[t[1]],
                       c = tile->content->vertices[t[2]];
            const double x = (a.x + b.x + c.x) / 3, y = (a.y + b.y + c.y) / 3;
            const auto cell = terrainCellOf(em.grid(), x, y);
            check(cell && (*cell)[0] == tile->content->key.row &&
                      (*cell)[1] == tile->content->key.column,
                  "every triangle's centroid is in its tile's cell");
            for (std::size_t depth = 1; depth <= tile->content->key.path.size(); ++depth)
                check(terrainQuadrantDigit(em.grid(), x, y, depth) ==
                          tile->content->key.path[depth - 1] - '0',
                      "and in its tile's quadrant");
        }
    std::cout << "topology: " << splits << " splits, " << paths.size() << " quadrant tiles\n";
}

void seamChecker() {
    const auto mesh = grid(80, 80);
    const auto set = terrainTileSet(mesh);
    check(set.tiles.size() > 4 && checkTerrainSeams(set.tiles).ok(),
          "a fresh tiling has sound seams");
    {
        auto tiles = set.tiles;
        // Find a vertex present in two tiles and change one copy.
        std::map<std::pair<double, double>, int> count;
        for (const auto &tile : tiles)
            for (const auto &p : tile->vertices)
                ++count[{p.x, p.y}];
        auto tile = std::make_shared<TerrainTileContent>(*tiles.at(0));
        for (auto &p : tile->vertices)
            if (count[{p.x, p.y}] > 1) {
                p.z += 1e-12;
                break;
            }
        tiles[0] = tile;
        const auto report = checkTerrainSeams(tiles);
        check(!report.ok() && report.zMismatches >= 1 && !report.problems.empty(),
              "copies with different Z are found");
    }
    {
        // A duplicated triangle is used twice in one direction.
        auto tiles = set.tiles;
        auto tile = std::make_shared<TerrainTileContent>(*tiles.at(0));
        tile->triangles.push_back(tile->triangles.front());
        tiles[0] = tile;
        check(checkTerrainSeams(tiles).nonManifoldEdges == 3, "a repeated triangle is not manifold");
    }
    {
        // A crack: split one triangle's edge on one side only (a T-junction).
        TerrainTileContent a, b;
        a.key = {0, 0, {}, 0};
        b.key = {0, 1, {}, 0};
        a.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
        a.triangles = {{0, 1, 2}};
        b.vertices = {{1, 0, 0}, {2, 0, 0}, {1, .5, 0}, {1, 1, 0}, {2, 1, 0}};
        b.triangles = {{0, 1, 2}, {2, 1, 4}, {2, 4, 3}};
        const auto report = checkTerrainSeams({std::make_shared<TerrainTileContent>(a),
                                               std::make_shared<TerrainTileContent>(b)});
        check(report.cracks >= 1 && !report.ok(), "a T-junction between tiles is a crack");
    }
    {
        // A crack in a boundary edge spanning far more cells than the grid's 4,096-cell box:
        // many short boundary edges elsewhere make the cell small.
        TerrainTileContent a, c;
        a.key = {0, 0, {}, 0};
        c.key = {0, 1, {}, 0};
        a.vertices = {{0, 0, 0}, {1000, 0, 0}, {500, 500, 0}};
        a.triangles = {{0, 1, 2}};
        c.vertices = {{0, 0, 0}, {500, -500, 0}, {500, 0, 0}, {1000, 0, 0}};
        c.triangles = {{0, 1, 2}, {2, 1, 3}};
        auto fine = std::make_shared<TerrainTileContent>(*terrainTileSet(grid(40, 40, .01, {2000, 0, 0})).tiles.at(0));
        fine->key = {0, 2, {}, 0};
        const auto report = checkTerrainSeams({std::make_shared<TerrainTileContent>(a),
                                               std::make_shared<TerrainTileContent>(c), fine});
        check(report.cracks >= 1, "a crack in a very long boundary edge is found");
    }
    std::cout << "seam checker: sound tiling, Z mismatch, non-manifold and cracks found\n";
}

void clusterParts() {
    // A dense cluster no 24 halvings separate: its cell splits into parts. The stored grid is
    // the grid the buckets were built with (R096.a review: the cell was halved once more), so
    // every triangle maps to its own tile's cell, and an edit there stays local.
    TerrainMesh mesh;
    auto add = [&](Vec3 a, Vec3 b, Vec3 c) {
        const auto base = std::uint32_t(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), {a, b, c});
        mesh.triangles.push_back({base, base + 1, base + 2});
    };
    add({-900000, -900000, 0}, {-899000, -900000, 1}, {-899500, -899000, 2});
    add({899000, 899000, 0}, {900000, 899000, 1}, {899500, 900000, 2});
    const auto dense = grid(35, 35, .0014, {100.01, 200.01, 5});
    const auto base = std::uint32_t(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), dense.vertices.begin(), dense.vertices.end());
    for (auto t : dense.triangles)
        mesh.triangles.push_back({t[0] + base, t[1] + base, t[2] + base});
    const auto tiling = tileTerrainTriangles(mesh);
    bool parts = false;
    for (const auto &key : tiling.keys)
        parts = parts || key.part > 0;
    check(parts, "the cluster splits into parts");
    Document doc;
    const auto created = createTerrain(doc, mesh, record(mesh));
    const auto grid0 = doc.bodies().at(created.terrain)->terrain->tiling;
    check(grid0.cell == tiling.cell, "the stored cell is the bucket cell");
    for (const auto id : created.tiles) {
        const auto content = terrainTileContent(*doc.bodies().at(id));
        for (const auto &t : content->triangles) {
            const auto a = content->vertices[t[0]], b = content->vertices[t[1]],
                       c = content->vertices[t[2]];
            const auto cell = terrainCellOf(grid0, (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3);
            check(cell && (*cell)[0] == content->key.row && (*cell)[1] == content->key.column,
                  "every triangle maps to its own tile's cell");
        }
    }
    // A split in the cluster rebuilds only tiles of its cell; the far triangles' tiles stay.
    TerrainTileKey clusterKey;
    for (const auto id : created.tiles)
        if (doc.bodies().at(id)->tileKey.part > 0)
            clusterKey = doc.bodies().at(id)->tileKey;
    const auto before = tilesOf(doc, created.terrain);
    const auto applied = applyTerrainEdit(doc, created.terrain, "test.split",
                                          kinds::split(100.01 + .0014 * 17.3, 200.01 + .0014 * 11.6),
                                          "Split");
    for (const auto id : applied.tiles.added) {
        const auto &key = doc.bodies().at(id)->tileKey;
        check(key.row == clusterKey.row && key.column == clusterKey.column,
              "the edit's tiles are in the cluster's cell");
    }
    for (const auto &tile : before)
        if (tile->tileKey.row != clusterKey.row || tile->tileKey.column != clusterKey.column)
            check(doc.bodies().at(tile->id) == tile, "tiles of other cells are untouched");
    check(checkTerrainSeams(doc, created.terrain).ok(), "seams hold");
    std::cout << "cluster parts: cell " << grid0.cell << ", " << created.tiles.size() << " tiles\n";
}

// A test-only entry whose storage is set by the test (the 8 MiB bound).
struct Heavy final : TerrainEditData {
    std::size_t size{};
    bool equals(const TerrainEditData &other) const override {
        const auto *o = dynamic_cast<const Heavy *>(&other);
        return o && o->size == size;
    }
    std::size_t bytes() const override { return size; }
};
void editIdsAndBounds() {
    registerTerrainEditKind({"test.heavy",
                             [](const TerrainEditMesh &, const TerrainEdit &) {
                                 return TerrainEditOutcome{TerrainRegionChange{}, {}};
                             },
                             {}});
    Document doc;
    const auto mesh = grid(40, 40);
    const auto created = createTerrain(doc, mesh, record(mesh));
    const auto key = doc.bodies().at(created.tiles.front())->tileKey;
    applyTerrainEdit(doc, created.terrain, "test.lift", kinds::lift(key, 1), "Lift");
    applyTerrainEdit(doc, created.terrain, "test.lift", kinds::lift(key, 1), "Lift");
    const auto &record1 = *doc.bodies().at(created.terrain)->terrain;
    check(record1.edits[1]->id == 2 && record1.nextEditId == 3, "IDs come from the counter");
    // A borderline entry refuses with LIMIT_EXCEEDED, counting what validation counts.
    auto heavy = std::make_shared<Heavy>();
    const auto overhead = terrainEditBytes(record1.edits) +
                          terrainEditEntryBytes(TerrainEdit{3, "test.heavy", nullptr});
    heavy->size = TerrainLimits::editBytes - overhead + 1;
    std::string code;
    check(!rejection([&] { applyTerrainEdit(doc, created.terrain, "test.heavy", heavy, "Heavy"); },
                     &code)
                   .empty() &&
              code == "LIMIT_EXCEEDED",
          "an entry one byte over the bound is LIMIT_EXCEEDED");
    heavy->size -= 1;
    check(rejection([&] { applyTerrainEdit(doc, created.terrain, "test.heavy", heavy, "Heavy"); })
              .empty(),
          "an entry exactly at the bound is accepted");
    doc.undo();
    // The last ID is refused rather than wrapped.
    auto body = std::make_shared<Body>(*doc.bodies().at(created.terrain));
    body->terrain->nextEditId = std::numeric_limits<std::uint32_t>::max();
    doc.apply({"Counter", {{created.terrain, doc.bodies().at(created.terrain), body}}},
              doc.revision());
    check(!rejection([&] {
              applyTerrainEdit(doc, created.terrain, "test.lift", kinds::lift(key, 1), "Lift");
          }, &code).empty() &&
              code == "LIMIT_EXCEEDED",
          "the last change ID refuses with LIMIT_EXCEEDED");
    std::cout << "edit IDs and bounds: counter, 8 MiB bound and last ID\n";
}

void editMeshCache() {
    // Entries are per document; deleted terrains are pruned and closed documents forgotten.
    auto &cache = terrainEditMeshCache();
    cache.clear();
    Document a, b;
    const auto meshA = grid(30, 30), meshB = grid(30, 30, 1, {0, 0, 50});
    const auto ta = createTerrain(a, meshA, record(meshA)).terrain;
    const auto tb = createTerrain(b, meshB, record(meshB)).terrain;
    check(ta == tb, "both documents use the same terrain ID");
    const std::vector<std::array<double, 2>> point{{10.5, 10.5}};
    const auto za = terrainElevationAt(a, ta, point).at(0).elevation,
               zb = terrainElevationAt(b, tb, point).at(0).elevation;
    check(std::abs(zb - za - 50) < 1e-9 && cache.size() == 2,
          "two documents with one terrain ID get their own entries");
    check(terrainElevationAt(a, ta, point).at(0).elevation == za, "and keep them");
    explodeTerrain(a, ta);
    cache.prune(a);
    check(cache.size() == 1, "a deleted terrain's entry is pruned");
    cache.forget(b.identity());
    check(cache.size() == 0, "a closed document's entries are forgotten");
    std::cout << "edit mesh cache: per document, pruned and forgotten\n";
}

void historyCharge() {
    // Many small edits fit the 512 MiB terrain budget because the record's sources and the
    // existing entries are shared: at least 64 one-tile edits stay undoable.
    Document doc;
    const int n = test::sanitized ? 160 : 320;
    const auto mesh = grid(n, n / 2);
    const auto created = createTerrain(doc, mesh, record(mesh, 120000));
    const auto terrain = created.terrain;
    const auto full = terrainRecordBytes(*doc.bodies().at(terrain)->terrain);
    check(full > 2'000'000, "the record is several MB");
    const auto initial = model(doc, false);
    const auto keys = [&] {
        std::vector<TerrainTileKey> result;
        for (const auto &tile : tilesOf(doc, terrain))
            result.push_back(tile->tileKey);
        return result;
    }();
    const int edits = 80;
    std::size_t largest = 0;
    for (int i = 0; i < edits; ++i) {
        const auto charged = doc.terrainHistoryBytes();
        applyTerrainEdit(doc, terrain, "test.lift",
                         kinds::lift(keys[std::size_t(i * 7) % keys.size()], .01 * (i + 1)), "Lift");
        largest = std::max(largest, doc.terrainHistoryBytes() - charged);
    }
    check(largest < full, "an edit is charged far less than the whole record");
    check(doc.history().entries.size() >= std::size_t(edits) + 1 &&
              doc.terrainHistoryBytes() <= Document::terrainHistoryLimit,
          "all edits and the creation stay in history");
    for (int i = 0; i < edits; ++i)
        doc.undo();
    check(model(doc, false) == initial, std::to_string(edits) + " undos give the original encoding");
    // terrainRecordChangeBytes counts only unshared parts.
    auto before = *doc.bodies().at(terrain)->terrain;
    auto after = before;
    after.edits.push_back(std::make_shared<TerrainEdit>(TerrainEdit{1, "test.lift", kinds::lift({}, 1)}));
    const auto change = terrainRecordChangeBytes(&before, &after);
    check(change < 4096 && terrainRecordChangeBytes(nullptr, &after) == terrainRecordBytes(after),
          "a shared source block is not charged; a creation is charged in full");
    std::cout << "history: record " << full << " bytes, largest edit charge " << largest
              << " bytes, " << edits << " edits undoable, terrain history "
              << doc.terrainHistoryBytes() << " bytes\n";
}

void regenerateIdRule() {
    // The generalised rule: a tile whose key and mesh are unchanged keeps its ID wherever the
    // change is (R095 kept only the leading run).
    Document doc;
    auto mesh = grid(120, 90);
    const auto created = createTerrain(doc, mesh, record(mesh));
    const auto terrain = created.terrain;
    const auto before = tilesOf(doc, terrain);
    // Move one vertex in the first cell: only tiles holding it change.
    mesh.vertices[1].z += .5;
    const auto regenerated = regenerateTerrain(doc, terrain, mesh, *doc.bodies().at(terrain)->terrain);
    check(regenerated.changed, "the regenerate changes the terrain");
    std::size_t kept = 0;
    for (const auto &tile : before)
        if (doc.bodies().contains(tile->id))
            ++kept;
    check(kept == before.size() - 1, "only the tile holding the moved vertex takes a new ID");
    const auto again = regenerateTerrain(doc, terrain, mesh, *doc.bodies().at(terrain)->terrain);
    check(!again.changed, "regenerating the same mesh changes nothing");
    std::cout << "regenerate: " << kept << " of " << before.size() << " tiles kept\n";
}

void elevationIndex() {
    // A translated terrain uses the index; the results equal R095's scan bit for bit, including
    // points on seams, vertices and outside. A rotated one falls back to the scan.
    Document doc;
    const auto mesh = grid(90, 90, .7, {-30, 12, 5});
    const auto created = createTerrain(doc, mesh, record(mesh));
    auto body = std::make_shared<Body>(*doc.bodies().at(created.terrain));
    std::vector<std::array<double, 2>> points = randomPoints(150, -40, 60, 3);
    for (int i = 0; i < 50; ++i)
        points.push_back({-30 + .7 * (i % 10) + 412.25, 12 + .7 * (i / 10) - 7.5}); // vertices
    for (const auto &transform :
         {Transform{}, Transform::translation({412.25, -7.5, 3}),
          Transform::rotation({0, 0, 1}, .3) * Transform::translation({5, 5, 0})}) {
        auto moved = std::make_shared<Body>(*doc.bodies().at(created.terrain));
        moved->transform = transform;
        Edit edit{"Move", {{created.terrain, doc.bodies().at(created.terrain), moved}}};
        doc.apply(std::move(edit), doc.revision());
        check(sameElevations(terrainElevationAt(doc, created.terrain, points),
                             scanElevation(doc, created.terrain, points)),
              "terrainElevationAt equals the full scan bit for bit");
    }
    std::cout << "elevation index: equal to the scan for 3 frames\n";
}
} // namespace

int main() {
    kinds::registerKinds();
    const std::vector<std::pair<const char *, void (*)()>> cases{
        {"stable tiling and digest", stableTilingAndDigest},
        {"format round trip", formatRoundTrip},
        {"legacy form", legacyForm},
        {"local replacement", localReplacement},
        {"topology and split", topologyAndSplit},
        {"seam checker", seamChecker},
        {"cluster parts", clusterParts},
        {"edit IDs and bounds", editIdsAndBounds},
        {"edit mesh cache", editMeshCache},
        {"history charge", historyCharge},
        {"regenerate ID rule", regenerateIdRule},
        {"elevation index", elevationIndex},
    };
    for (const auto &[name, run] : cases) {
        try {
            const auto start = std::chrono::steady_clock::now();
            run();
            std::cout << "  ok " << name << " ("
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                      << " s)\n";
        } catch (const std::exception &error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    return 0;
}
