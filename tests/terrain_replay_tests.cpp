// R096.a part 2 (ADR 0169 §2, §5, §6): the replay engine and the commands that replay
// (terrain.regenerate, terrain.set_parameters, terrain.reset_edits, terrain.remove_edit), progress
// and cancel for replay, and the per-tile surface and contour caches. Terrains are imported from
// a generated point file, so their base sources reproduce their base mesh; the test-only kinds of
// terrain_edit_test_kinds.hpp stand in for R096.b–e's operations.
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain_edit.hpp"
#include "io/document_io.hpp"
#include "terrain_edit_test_kinds.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
using namespace sketchy;
namespace kinds = terrain_test_kinds;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string refusalCode(F operation) {
    try {
        operation();
    } catch (const TerrainError &error) {
        return error.code();
    } catch (const InspectionError &error) {
        return error.code();
    } catch (const std::exception &error) {
        return std::string("other: ") + error.what();
    }
    return {};
}
// About 2n triangles from n random points, as terrain_budget_benchmark's source.
QByteArray pointSource(int points, std::uint32_t seed = 20261019) {
    QByteArray file;
    auto random = [&] {
        seed = seed * 1664525u + 1013904223u;
        return double(seed >> 8) / double(1u << 24);
    };
    for (int i = 0; i < points; ++i) {
        const double x = random() * 400, y = random() * 300;
        const double z = 120 + 6 * std::sin(x / 40) * std::cos(y / 30) + 0.01 * x;
        file += QByteArray::number(x, 'f', 3) + ' ' + QByteArray::number(y, 'f', 3) + ' ' +
                QByteArray::number(z, 'f', 3) + '\n';
    }
    return file;
}
Id importPoints(Document &doc, int points = 12000) {
    const QJsonObject command{{"format", "points"}, {"units", "m"}, {"columns", "XYZ"},
                              {"delimiter", "whitespace"}, {"header", false}};
    const auto result = importTerrainSource(doc, pointSource(points), command);
    return result["terrain"].toString().toULongLong();
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
QJsonObject command(const char *name, Id terrain, QJsonObject fields = {}) {
    fields["command"] = name;
    fields["terrain"] = QString::number(terrain);
    return fields;
}
QByteArray model(const Document &doc, bool allocator = true) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json.remove("revision");
    if (!allocator) {
        json.remove("nextId");
        json.remove("documentId");
    }
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}
std::vector<BodyPtr> tilesOf(const Document &doc, Id terrain) {
    std::vector<BodyPtr> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(doc.bodies().at(id));
    return tiles;
}
// The tiles as (key -> name and mesh), for comparing two documents' terrains.
std::map<TerrainTileKey, TerrainTileContent> contents(const Document &doc, Id terrain) {
    std::map<TerrainTileKey, TerrainTileContent> result;
    for (const auto &tile : tilesOf(doc, terrain))
        result[tile->tileKey] = *terrainTileContent(*tile);
    return result;
}
bool sameContents(const std::map<TerrainTileKey, TerrainTileContent> &a,
                  const std::map<TerrainTileKey, TerrainTileContent> &b) {
    if (a.size() != b.size())
        return false;
    for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j)
        if (i->first != j->first || i->second.name != j->second.name || !i->second.sameMesh(j->second))
            return false;
    return true;
}
// A sequence of edits: lifts, splits, and a refinement that splits a cell into quadrants.
struct Script {
    std::vector<std::pair<std::string, std::shared_ptr<const TerrainEditData>>> edits;
};
Script script(const Document &doc, Id terrain) {
    Script s;
    const auto tiles = tilesOf(doc, terrain);
    const auto grid = doc.bodies().at(terrain)->terrain->tiling;
    s.edits.push_back({"test.lift", kinds::lift(tiles.at(3)->tileKey, 1.5)});
    s.edits.push_back({"test.lift", kinds::lift(tiles.at(tiles.size() / 2)->tileKey, -.75)});
    for (int i = 0; i < 8; ++i)
        s.edits.push_back({"test.split", kinds::split(grid.originX + 37.3 + 29.1 * i,
                                                      grid.originY + 41.7 + 23.3 * i)});
    const double cx = grid.originX + 2.5 * grid.cell, cy = grid.originY + 1.5 * grid.cell;
    s.edits.push_back({"test.refine", kinds::refine(cx, cy, 7)});
    s.edits.push_back({"test.lift", kinds::lift(tiles.at(5)->tileKey, .25)});
    return s;
}
void apply(Document &doc, Id terrain, const Script &s, std::size_t skip = SIZE_MAX) {
    for (std::size_t i = 0; i < s.edits.size(); ++i)
        if (i != skip)
            applyTerrainEdit(doc, terrain, s.edits[i].first, s.edits[i].second, "Edit");
}

void replayEqualsIncremental() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto s = script(doc, terrain);
    apply(doc, terrain, s);
    const auto &record = *doc.bodies().at(terrain)->terrain;
    check(record.edits.size() == s.edits.size(), "every edit is recorded");
    // The replay engine on the base gives exactly the document's tiles.
    const auto request = parseTerrainReplayRequest(doc, command("terrain.regenerate", terrain));
    const auto result = runTerrainReplay(request);
    check(result.ok && result.tileSet, "the replay succeeds");
    std::map<TerrainTileKey, TerrainTileContent> replayed;
    for (const auto &tile : result.tileSet->tiles)
        replayed[tile->key] = *tile;
    check(sameContents(replayed, contents(doc, terrain)),
          "replay == incremental: the same keys, names and meshes");
    const auto report = result.report["terrain"].toObject()["edits"].toObject();
    check(report["count"] == double(s.edits.size()) && report["applied"] == double(s.edits.size()) &&
              report["skipped"] == 0,
          "the report counts the replayed edits");
    // C7: Regenerate after edits is a no-op (identical record, IDs and digest).
    const auto before = model(doc);
    const auto revision = doc.revision();
    const auto regenerated = regenerateTerrain(doc, terrain, *result.tileSet, result.record);
    check(!regenerated.changed && doc.revision() == revision && model(doc) == before,
          "Regenerate after edits changes nothing");
    // Through the batch API a no-op regenerate commits nothing, which the batch refuses.
    check(!refusalCode([&] { run(doc, command("terrain.regenerate", terrain)); }).empty() &&
              doc.revision() == revision,
          "the regenerate command changes nothing");
    // After save and reopen, too.
    auto reopened = decodeDocument(encodeDocument(doc));
    const auto again = runTerrainReplay(parseTerrainReplayRequest(reopened, command("terrain.regenerate", terrain)));
    check(!regenerateTerrain(reopened, terrain, *again.tileSet, again.record).changed,
          "Regenerate after reopen changes nothing");
    // Determinism: a second document with the same import and edits is identical.
    Document other;
    const auto copy = importPoints(other);
    apply(other, copy, script(other, copy));
    check(model(other, false) == model(doc, false), "the same edits give identical documents");
    std::cout << "replay: " << s.edits.size() << " edits, " << result.tileSet->tiles.size()
              << " tiles, regenerate is a no-op\n";
}

void resetEdits() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto originalDigest = doc.bodies().at(terrain)->terrain->meshDigest;
    const auto original = contents(doc, terrain);
    const auto untouchedBefore = tilesOf(doc, terrain);
    check(refusalCode([&] { run(doc, command("terrain.reset_edits", terrain)); }) ==
              "INVALID_REQUEST",
          "reset with no edits refuses");
    const auto s = script(doc, terrain);
    apply(doc, terrain, s);
    const auto edited = model(doc, false);
    const auto editedTiles = tilesOf(doc, terrain);
    const auto operation = run(doc, command("terrain.reset_edits", terrain));
    check(operation["changed"].toBool() && doc.history().entries.back().label == "Reset terrain edits",
          "reset is one labelled edit");
    const auto &record = *doc.bodies().at(terrain)->terrain;
    check(record.edits.empty() && record.meshDigest == originalDigest &&
              sameContents(contents(doc, terrain), original),
          "reset returns the base terrain");
    // Tiles no edit touched keep their bodies.
    std::size_t kept = 0;
    for (const auto &tile : editedTiles)
        if (doc.bodies().contains(tile->id) && doc.bodies().at(tile->id) == tile)
            ++kept;
    check(kept > editedTiles.size() / 2, "untouched tiles keep their bodies and IDs");
    const auto reset = model(doc);
    doc.undo();
    check(model(doc, false) == edited && tilesOf(doc, terrain) == editedTiles,
          "undo restores the edits, the tiles pointer-identical");
    doc.redo();
    check(model(doc) == reset, "redo resets again");
    (void)untouchedBefore;
    std::cout << "reset: " << kept << " of " << editedTiles.size() << " tiles kept\n";
}

void removeEdit() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto s = script(doc, terrain);
    apply(doc, terrain, s);
    const auto edited = model(doc, false);
    const auto ids = [&] {
        std::vector<std::uint32_t> list;
        for (const auto &edit : doc.bodies().at(terrain)->terrain->edits)
            list.push_back(edit->id);
        return list;
    }();
    // Remove a split in the middle: the result is the other edits applied incrementally.
    const std::size_t middle = 4;
    run(doc, command("terrain.remove_edit", terrain, {{"edit", double(ids[middle])}}));
    check(doc.history().entries.back().label == "Remove terrain change", "one labelled edit");
    Document expected;
    const auto copy = importPoints(expected);
    apply(expected, copy, script(expected, copy), middle);
    check(sameContents(contents(doc, terrain), contents(expected, copy)) &&
              doc.bodies().at(terrain)->terrain->meshDigest ==
                  expected.bodies().at(copy)->terrain->meshDigest,
          "removing an entry equals never having made it");
    std::vector<std::uint32_t> remaining;
    for (const auto &edit : doc.bodies().at(terrain)->terrain->edits)
        remaining.push_back(edit->id);
    auto without = ids;
    without.erase(without.begin() + std::ptrdiff_t(middle));
    check(remaining == without, "the other entries keep their IDs and order");
    const auto removed = model(doc);
    doc.undo();
    check(model(doc, false) == edited, "undo restores the entry and the tiles");
    doc.redo();
    check(model(doc) == removed, "redo removes it again");
    // Refusals change nothing.
    const auto revision = doc.revision();
    check(refusalCode([&] {
              run(doc, command("terrain.remove_edit", terrain, {{"edit", 999}}));
          }) == "NOT_FOUND",
          "an unknown change is NOT_FOUND");
    check(refusalCode([&] {
              run(doc, command("terrain.remove_edit", terrain, {{"edit", "1"}}));
          }) == "INVALID_REQUEST",
          "an edit ID is an integer");
    {
        auto locked = std::make_shared<Body>(*doc.bodies().at(terrain));
        locked->locked = true;
        doc.apply({"Lock", {{terrain, doc.bodies().at(terrain), locked}}}, doc.revision());
        check(refusalCode([&] {
                  run(doc, command("terrain.remove_edit", terrain, {{"edit", double(ids[0])}}));
              }) == "INVALID_OPERATION",
              "a locked terrain refuses");
        doc.undo();
    }
    check(model(doc) == removed && doc.revision() == revision + 2, "refusals change nothing");
    // IDs are never reused: removing the last change and adding another gives a new ID, and
    // a reset keeps the counter.
    const auto lastId = doc.bodies().at(terrain)->terrain->edits.back()->id;
    run(doc, command("terrain.remove_edit", terrain, {{"edit", double(lastId)}}));
    applyTerrainEdit(doc, terrain, "test.lift", kinds::lift(tilesOf(doc, terrain).front()->tileKey, .1),
                     "Lift");
    check(doc.bodies().at(terrain)->terrain->edits.back()->id == lastId + 1,
          "a removed change's ID is not reused");
    run(doc, command("terrain.reset_edits", terrain));
    check(doc.bodies().at(terrain)->terrain->edits.empty() &&
              doc.bodies().at(terrain)->terrain->nextEditId == lastId + 2,
          "Reset edits keeps the ID counter");
    for (std::size_t i = 0; i < 3; ++i)
        doc.undo();
    // Removing the refinement leaves a later lift of a quadrant tile with no tile: it is skipped
    // with a located notice and kept in the list.
    Document quad;
    const auto t = importPoints(quad);
    const auto qs = script(quad, t);
    apply(quad, t, qs);
    TerrainTileKey quadrant;
    for (const auto &tile : tilesOf(quad, t))
        if (!tile->tileKey.path.empty())
            quadrant = tile->tileKey;
    check(!quadrant.path.empty(), "the refinement made quadrant tiles");
    applyTerrainEdit(quad, t, "test.lift", kinds::lift(quadrant, .5), "Lift quadrant");
    const auto refineId = quad.bodies().at(t)->terrain->edits.at(10)->id;
    const auto operation = run(quad, command("terrain.remove_edit", t, {{"edit", double(refineId)}}));
    const auto report = operation["terrainReport"].toObject()["terrain"].toObject();
    bool skipped = false;
    for (const auto &kind : report["notices"].toArray())
        if (kind.toObject()["kind"] == "edit-skipped" &&
            kind.toObject()["locations"].toArray().at(0).toObject()["reference"].toString().startsWith("edit "))
            skipped = true;
    check(skipped && report["edits"].toObject()["skipped"] == 1 &&
              quad.bodies().at(t)->terrain->edits.size() == qs.edits.size(),
          "a change that no longer applies is skipped with a notice and kept");
    std::cout << "remove: equals the incremental result; skipped entry reported\n";
}

void setParametersReplays() {
    Document doc;
    const auto terrain = importPoints(doc, 6000);
    const auto s = script(doc, terrain);
    apply(doc, terrain, s);
    const auto operation =
        run(doc, command("terrain.set_parameters", terrain, {{"flipFlatTriangles", false}}));
    const auto report = operation["terrainReport"].toObject()["terrain"].toObject();
    check(doc.bodies().at(terrain)->terrain->edits.size() == s.edits.size() &&
              report["edits"].toObject()["count"] == double(s.edits.size()),
          "Terrain settings replays the edit list on the new base");
    check(!doc.bodies().at(terrain)->terrain->parameters.flipFlatTriangles,
          "the parameters change");
    // describe lists the edits.
    const auto described = terrainDescription(doc, terrain)["edits"].toObject();
    check(described["count"] == double(s.edits.size()) &&
              described["entries"].toArray().size() == int(s.edits.size()) &&
              described["entries"].toArray()[0].toObject()["kind"] == "test.lift" &&
              described["entries"].toArray()[2].toObject()["bounds"].toArray().size() == 4,
          "terrain.describe reports the edits");
    std::cout << "set_parameters: replays " << s.edits.size() << " edits\n";
}

void cancelDuringReplay() {
    Document doc;
    const auto terrain = importPoints(doc, 6000);
    const auto tiles = tilesOf(doc, terrain);
    for (int i = 0; i < 40; ++i)
        applyTerrainEdit(doc, terrain, "test.lift",
                         kinds::lift(tiles[std::size_t(i) % tiles.size()]->tileKey, .01 * (i + 1)),
                         "Lift");
    const auto before = model(doc);
    const auto revision = doc.revision();
    const auto entries = doc.history().entries.size();
    const auto request = parseTerrainReplayRequest(doc, command("terrain.reset_edits", terrain));
    // Count the replay checkpoints of an uncancelled run, and check progress moves forward.
    int replayReports = 0;
    double last = -1;
    bool forward = true;
    TerrainProgress counting([] { return false; }, [&](TerrainStage stage, double fraction) {
        const auto overall = terrainReplayProgress(stage, fraction);
        forward = forward && overall >= last;
        last = overall;
        replayReports += stage == TerrainStage::Replay;
    });
    // reset_edits replays nothing; Regenerate replays all 40.
    const auto full =
        parseTerrainReplayRequest(doc, command("terrain.regenerate", terrain));
    const auto uncancelled = runTerrainReplay(full, &counting);
    check(uncancelled.ok && forward && replayReports >= 2 && last == 1,
          "a replay reports its progress forward to the end");
    const auto plain = runTerrainReplay(full);
    check(plain.tileSet->tiles.size() == uncancelled.tileSet->tiles.size(),
          "progress does not change the result");
    for (std::size_t i = 0; i < plain.tileSet->tiles.size(); ++i)
        check(plain.tileSet->tiles[i]->sameMesh(*uncancelled.tileSet->tiles[i]),
              "progress does not change the result");
    // Cancel at every replay entry: the run returns cancelled and the document is unchanged.
    int cancels = 0;
    for (int at = 1; at <= 40; at += 3) {
        int seen = 0;
        bool requested = false;
        TerrainProgress progress([&] { return requested; },
                                 [&](TerrainStage stage, double) {
                                     if (stage == TerrainStage::Replay && ++seen >= at)
                                         requested = true;
                                 });
        auto result = runTerrainReplay(full, &progress);
        if (!requested)
            continue; // fewer reports than `at` (reports are thinned to 1/256 steps)
        check(result.cancelled && !result.ok && result.code == "CANCELLED" && !result.tileSet,
              "a cancelled replay returns cancelled with nothing kept");
        check(refusalCode([&] { applyTerrainReplay(doc, full, std::move(result)); }) ==
                  "CANCELLED",
              "a cancelled replay is not applied");
        ++cancels;
    }
    check(cancels > 0, "cancels were exercised");
    // The base tiling reports its progress and is cancellable too.
    {
        bool requested = false, tileSeen = false;
        TerrainProgress progress([&] { return requested; },
                                 [&](TerrainStage stage, double fraction) {
                                     if (stage == TerrainStage::Tile && fraction > 0 && fraction < 1) {
                                         tileSeen = true;
                                         requested = true;
                                     }
                                 });
        const auto result = runTerrainReplay(full, &progress);
        check(tileSeen && result.cancelled, "a cancel while tiling the base is honoured");
    }
    check(model(doc) == before && doc.revision() == revision &&
              doc.history().entries.size() == entries,
          "cancel leaves revision, history and the document unchanged");
    // A request whose terrain changed in the meantime is refused.
    const auto stale = runTerrainReplay(request);
    applyTerrainEdit(doc, terrain, "test.lift", kinds::lift(tiles[0]->tileKey, 1), "Lift");
    check(refusalCode([&] { applyTerrainReplay(doc, request, stale); }) == "INVALID_OPERATION",
          "a stale replay is refused");
    std::cout << "cancel: " << cancels << " cancelled replays left the document unchanged\n";
}

void perTileDisplay() {
    Document doc;
    const auto terrain = importPoints(doc);
    auto display = doc.bodies().at(terrain)->terrain->display;
    display.contours = true;
    display.contourInterval = .5;
    setTerrainDisplay(doc, terrain, display);
    TerrainSurfaceCache surfaces;
    TerrainContourCache contours;
    auto full = [&] {
        std::vector<const Body *> records;
        for (const auto &tile : tilesOf(doc, terrain))
            records.push_back(tile.get());
        return records;
    };
    auto equalToFull = [&](const TerrainTileSurfaces &perTile) {
        const auto records = full();
        const auto reference = terrainSurfaceDisplay(records);
        for (const auto *tile : records) {
            const auto &surface = *perTile.tile(tile->id);
            for (const auto &[id, p] : tile->surface.vertices) {
                const auto a = surface.normal(p, {}), b = reference.normal(p, {});
                if (std::bit_cast<std::uint64_t>(a.x) != std::bit_cast<std::uint64_t>(b.x) ||
                    std::bit_cast<std::uint64_t>(a.y) != std::bit_cast<std::uint64_t>(b.y) ||
                    std::bit_cast<std::uint64_t>(a.z) != std::bit_cast<std::uint64_t>(b.z))
                    return false;
            }
            for (const auto &[id, edge] : tile->topology.edges)
                if (surface.outerEdge(id) != reference.outerEdge(tile->id, id))
                    return false;
        }
        return perTile.zMin == reference.zMin && perTile.zMax == reference.zMax;
    };
    const auto first = surfaces.get(doc, terrain);
    const auto firstContours = contours.get(doc, terrain);
    check(equalToFull(*first), "per-tile surfaces equal the whole-terrain surface bit for bit");
    {
        const auto reference = terrainContours(full(), .5, terrainElevationOffset(doc, terrain));
        check(firstContours->segments.size() == reference.segments.size() &&
                  firstContours->levels == reference.levels,
              "per-tile contours equal the whole-terrain contours");
    }
    const auto computed = surfaces.tileComputations();
    const auto generated = contours.tileGenerations();
    const auto tiles = tilesOf(doc, terrain);
    const auto target = tiles.at(tiles.size() / 2);
    applyTerrainEdit(doc, terrain, "test.lift", kinds::lift(target->tileKey, 2), "Lift");
    const auto second = surfaces.get(doc, terrain);
    check(equalToFull(*second), "after an edit, too");
    const auto recomputed = surfaces.tileComputations() - computed;
    check(recomputed >= 1 && recomputed < tiles.size() / 2,
          "only the rebuilt tile and its seam ring are recomputed");
    std::size_t same = 0;
    for (const auto &tile : tilesOf(doc, terrain))
        same += first->tile(tile->id) && first->tile(tile->id) == second->tile(tile->id);
    check(same + recomputed == tiles.size(), "every other tile keeps its surface object");
    const auto secondContours = contours.get(doc, terrain);
    check(contours.tileGenerations() - generated == 1 && secondContours != firstContours,
          "only the rebuilt tile's contours are generated");
    const auto reference = terrainContours(full(), .5, terrainElevationOffset(doc, terrain));
    bool equal = secondContours->segments.size() == reference.segments.size();
    for (std::size_t i = 0; equal && i < reference.segments.size(); ++i)
        equal = secondContours->segments[i].a == reference.segments[i].a &&
                secondContours->segments[i].b == reference.segments[i].b;
    check(equal, "the assembled contours equal the whole-terrain contours, in key order");
    std::cout << "display: " << recomputed << " of " << tiles.size()
              << " tile surfaces recomputed after a one-tile edit\n";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    kinds::registerKinds();
    const std::vector<std::pair<const char *, void (*)()>> cases{
        {"replay equals incremental", replayEqualsIncremental},
        {"reset edits", resetEdits},
        {"remove edit", removeEdit},
        {"set_parameters replays", setParametersReplays},
        {"cancel during replay", cancelDuringReplay},
        {"per-tile display", perTileDisplay},
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
