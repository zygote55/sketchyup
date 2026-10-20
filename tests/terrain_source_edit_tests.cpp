// R096.c (ADR 0169 §3, §4.2, §6, §8): cavity re-triangulation and contour edit. The three
// commands terrain.edit_source, terrain.add_source (with promotion of a display contour) and
// terrain.remove_source as edit kinds that replay exactly; validation and located refusals; the
// elevation-only path; stable source ids; and checks C1, C2, C5, C6 and C7 after the edits, after
// Undo and Redo, after save and reopen and after Regenerate.
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sources.hpp"
#include "io/document_io.hpp"
#include "io/terrain_io.hpp"
#include "terrain_import_fixtures.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <numbers>
#include <set>
using namespace sketchy;
namespace fixture = sketchy::terrain_fixture;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Refusal {
    std::string code, message;
    QJsonObject report;
};
template <class F> Refusal refusal(F operation) {
    try {
        operation();
    } catch (const TerrainImportError &error) {
        return {error.code(), error.what(), error.report()};
    } catch (const TerrainError &error) {
        return {error.code(), error.what(), {}};
    } catch (const InspectionError &error) {
        return {error.code(), error.what(), {}};
    } catch (const std::exception &error) {
        return {"other", error.what(), {}};
    }
    return {};
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
// One command through the batch API; a refusal comes back as the batch's error.
QJsonObject run(Document &doc, QJsonObject command) {
    const auto response = executeBatch(doc, batch(doc, {command}));
    return response["terrainOperations"].toArray().at(0).toObject();
}
// The same command applied directly (to see its TerrainImportError and report).
QJsonObject direct(Document &doc, QJsonObject command) {
    QJsonObject operation;
    executeTerrainCommand(doc, command, &operation);
    return operation;
}
QJsonObject command(const char *name, Id terrain, QJsonObject fields = {}) {
    fields["command"] = name;
    fields["terrain"] = QString::number(terrain);
    return fields;
}
QByteArray encoded(const Document &doc) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json.remove("revision");
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}
QByteArray model(const Document &doc) {
    auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
    json.remove("revision");
    json.remove("nextId");
    json.remove("documentId");
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}
std::map<Id, BodyPtr> tilesOf(const Document &doc, Id terrain) {
    std::map<Id, BodyPtr> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles[id] = doc.bodies().at(id);
    return tiles;
}
Vec3 toModel(const Document &doc, Id terrain, Vec3 local) {
    return doc.worldTransform(terrain).point(local);
}
// The centre of the terrain's plan box in its frame (the hill's centre for the rings).
Vec3 centre(const Document &doc, Id terrain) {
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const auto id : terrainTiles(doc, terrain))
        for (const auto &[vid, p] : doc.bodies().at(id)->surface.vertices) {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
    return {(minX + maxX) / 2, (minY + maxY) / 2, 0};
}
// Points given relative to that centre, in the terrain frame.
std::vector<Vec3> rel(const Document &doc, Id terrain, std::vector<Vec3> points) {
    const auto c = centre(doc, terrain);
    for (auto &p : points)
        p = {c.x + p.x, c.y + p.y, p.z};
    return points;
}
// A point moved radially from the centre by `scale`.
Vec3 radial(const Vec3 &p, const Vec3 &c, double scale) {
    return {c.x + (p.x - c.x) * scale, c.y + (p.y - c.y) * scale, p.z};
}
QJsonArray modelPoints(const Document &doc, Id terrain, const std::vector<Vec3> &local,
                       bool withZ = true) {
    QJsonArray points;
    for (const auto &p : local) {
        const auto m = toModel(doc, terrain, p);
        points.append(withZ ? QJsonArray{m.x, m.y, m.z} : QJsonArray{m.x, m.y});
    }
    return points;
}
const TerrainRecord &recordOf(const Document &doc, Id terrain) {
    return *doc.bodies().at(terrain)->terrain;
}
std::shared_ptr<const TerrainSourceState> stateOf(const Document &doc, Id terrain) {
    return terrainSourceState(recordOf(doc, terrain));
}

// ---- Terrains -----------------------------------------------------------------------------
// A hill of closed elliptic contour rings, 3 m apart and 0.5 m lower each.
QByteArray ringSource(int rings = 40, int vertices = 160) {
    QByteArray out = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n6\n0\n"
                     "ENDSEC\n0\nSECTION\n2\nENTITIES\n";
    for (int k = 0; k < rings; ++k) {
        const double r = 10 + 3 * k, z = 105 - 0.5 * k;
        out += "0\nLWPOLYLINE\n5\n" + QByteArray::number(0x100 + k, 16).toUpper() +
               "\n8\nCONTOURS\n90\n" + QByteArray::number(vertices) + "\n70\n1\n38\n" +
               QByteArray::number(z, 'f', 4) + '\n';
        for (int i = 0; i < vertices; ++i) {
            const double t = 2 * std::numbers::pi * i / vertices + 0.05 * k;
            out += "10\n" + QByteArray::number(r * std::cos(t) * 1.3, 'f', 4) + "\n20\n" +
                   QByteArray::number(r * std::sin(t), 'f', 4) + '\n';
        }
    }
    out += "0\nENDSEC\n0\nEOF\n";
    return out;
}
Id importRings(Document &doc, int rings = 40, int vertices = 160) {
    const QJsonObject options{{"format", "dxf"}, {"units", "header"}};
    const auto result = importTerrainSource(doc, ringSource(rings, vertices), options);
    return result["terrain"].toString().toULongLong();
}
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
    const QJsonObject options{{"format", "points"}, {"units", "m"}, {"columns", "XYZ"},
                              {"delimiter", "whitespace"}, {"header", false}};
    const auto result = importTerrainSource(doc, pointSource(points), options);
    return result["terrain"].toString().toULongLong();
}
// The contour sources of a terrain, by elevation (one ring per elevation on the hill), as
// copies: a state the caller does not hold may be updated in place by the next edit.
std::map<double, std::shared_ptr<const TerrainSource>> contours(const TerrainSourceState &state) {
    std::map<double, std::shared_ptr<const TerrainSource>> result;
    for (const auto *source : state.sources())
        if (source->role == TerrainSourceRole::Contour)
            result[source->points.front().z] = std::make_shared<const TerrainSource>(*source);
    return result;
}

// ---- Checks C1, C2, C5, C6, C7 ---------------------------------------------------------------
void checkC1(const Document &doc, Id terrain, const std::string &when) {
    const auto c1 = checkTerrainSourceConsistency(doc, terrain);
    check(c1.ok() && (c1.segments > 0 || !c1.sources),
          when + ": C1, the current contours lie on the surface (" +
              (c1.problems.empty() ? std::string("no segments") : c1.problems.front()) + ")");
}
void checkC6(const Document &doc, Id terrain, const std::string &when) {
    const auto seams = checkTerrainSeams(doc, terrain);
    check(seams.ok(), when + ": C6, seams hold (" +
                          (seams.problems.empty() ? std::string() : seams.problems.front()) + ")");
}
double segmentDistance(Vec3 p, Vec3 a, Vec3 b) {
    const double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
    double t = len > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(a.x + t * dx - p.x, a.y + t * dy - p.y);
}
// C2: display contours at each current contour's elevation pass through it within 1e-9 m, and
// the incrementally updated cache equals a fresh one (no stale tiles).
void checkC2(const Document &doc, Id terrain, TerrainContourCache &cache, const std::string &when) {
    const auto cached = cache.get(doc, terrain);
    TerrainContourCache fresh;
    const auto rebuilt = fresh.get(doc, terrain);
    check(cached->segments.size() == rebuilt->segments.size(),
          when + ": C2, the cached display contours are not stale");
    for (std::size_t i = 0; i < cached->segments.size(); ++i)
        check(cached->segments[i].a == rebuilt->segments[i].a &&
                  cached->segments[i].b == rebuilt->segments[i].b,
              when + ": C2, the cached display contours equal a fresh build");
    std::size_t checked = 0;
    const auto state = stateOf(doc, terrain);
    for (const auto *source : state->sources()) {
        if (source->role != TerrainSourceRole::Contour)
            continue;
        const double level = source->points.front().z;
        const double k = (level + rebuilt->elevationOffset) / rebuilt->interval;
        if (k != std::round(k))
            continue;
        std::vector<const TerrainContourSegment *> at;
        for (const auto &segment : rebuilt->segments)
            if (std::abs(segment.a.z - level) <= 1e-9)
                at.push_back(&segment);
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        for (const auto &p : source->points) {
            // A display line passes a vertex where the ground falls below the level beside it.
            bool lower = false;
            for (const auto &ref : mesh->trianglesAt(p)) {
                const auto &content = *mesh->tile(ref.tile)->content;
                for (const auto v : content.triangles[ref.triangle])
                    lower = lower || content.vertices[v].z < level;
            }
            if (!lower)
                continue;
            double best = 1e300;
            for (const auto *segment : at)
                best = std::min(best, segmentDistance(p, segment->a, segment->b));
            check(best <= 1e-9, when + ": C2, the display contour passes through source " +
                                    std::to_string(source->id));
            ++checked;
        }
    }
    check(checked > 0, when + ": C2 checked some contour vertices");
}
// C7: Regenerate after the edits changes nothing (record, IDs, digest), also through a replay
// that sees only the record.
void checkC7(Document &doc, Id terrain, const std::string &when) {
    const auto before = encoded(doc);
    const auto revision = doc.revision();
    const auto request = parseTerrainReplayRequest(doc, command("terrain.regenerate", terrain));
    auto result = runTerrainReplay(request);
    check(result.ok, when + ": C7, the replay succeeds: " + result.message);
    const auto edits = result.report["terrain"].toObject()["edits"].toObject();
    check(edits["skipped"] == 0, when + ": C7, no entry is skipped");
    std::string detail;
    {
        std::map<TerrainTileKey, TerrainTilePtr> replayed;
        for (const auto &tile : result.tileSet->tiles)
            replayed[tile->key] = tile;
        std::size_t differ = 0, missing = 0;
        for (const auto &[id, tile] : tilesOf(doc, terrain)) {
            const auto found = replayed.find(tile->tileKey);
            if (found == replayed.end())
                ++missing;
            else if (!found->second->sameMesh(*terrainTileContent(*tile)))
                ++differ;
        }
        detail = " (tiles " + std::to_string(tilesOf(doc, terrain).size()) + " vs " +
                 std::to_string(replayed.size()) + ", differ " + std::to_string(differ) +
                 ", missing " + std::to_string(missing) + ", record " +
                 (result.record == recordOf(doc, terrain) ? "same" : "differs") + ")";
    }
    const auto tilesBefore = tilesOf(doc, terrain);
    const auto recordBefore = recordOf(doc, terrain);
    const auto applied = applyTerrainReplay(doc, request, std::move(result));
    if (applied["changed"].toBool()) {
        const auto tilesAfter = tilesOf(doc, terrain);
        const auto &r = recordOf(doc, terrain);
        detail += std::string(" record: sources ") + (r.sources == recordBefore.sources ? "=" : "!=") +
                  " digest " + (r.meshDigest == recordBefore.meshDigest ? "=" : "!=") + " tiling " +
                  (r.tiling == recordBefore.tiling ? "=" : "!=") + " edits " +
                  (r.edits.size() == recordBefore.edits.size() ? "=" : "!=") + " generator " +
                  (r.generator == recordBefore.generator ? "=" : "!=") + " all " +
                  (r == recordBefore ? "=" : "!=");
        for (std::size_t i = 0; i < r.edits.size() && i < recordBefore.edits.size(); ++i)
            if (!(*r.edits[i] == *recordBefore.edits[i]))
                detail += " edit " + std::to_string(i) + " differs";
        for (const auto &[id, tile] : tilesAfter) {
            const auto old = tilesBefore.find(id);
            if (old == tilesBefore.end()) {
                detail += " new tile " + std::to_string(id);
                continue;
            }
            const auto &a = *old->second, &b = *tile;
            detail += std::string(" name ") + (a.name == b.name ? "=" : "!=") + " surface " +
                      (a.surface.vertices == b.surface.vertices ? "=" : "!=") + " materials " +
                      (a.materials == b.materials ? "=" : "!=") + " color " +
                      (a.color == b.color ? "=" : "!=") + " key " +
                      (a.tileKey == b.tileKey ? "=" : "!=") + " faces " +
                      (a.surface.faces == b.surface.faces ? "=" : "!=") + " edges " +
                      (a.edgeAppearances == b.edgeAppearances ? "=" : "!=");
        }
    }
    check(!applied["changed"].toBool(), when + ": C7, Regenerate changes no tile or record" + detail);
    check(doc.revision() == revision && encoded(doc) == before,
          when + ": C7, Regenerate is a no-op");
}
// C5: the tiles the edit did not replace are pointer-identical with their IDs, and elevations
// outside the replaced tiles' boxes are bit-equal.
struct Snapshot {
    std::map<Id, BodyPtr> tiles;
    std::vector<std::array<double, 2>> probes;
    std::vector<TerrainElevation> elevations;
};
std::vector<TerrainElevation> elevationsAt(const Document &doc, Id terrain,
                                           const std::vector<std::array<double, 2>> &probes) {
    std::vector<TerrainElevation> result;
    for (std::size_t i = 0; i < probes.size(); i += terrainPointBatch) {
        const std::vector<std::array<double, 2>> part(
            probes.begin() + std::ptrdiff_t(i),
            probes.begin() + std::ptrdiff_t(std::min(probes.size(), i + terrainPointBatch)));
        const auto found = terrainElevationAt(doc, terrain, part);
        result.insert(result.end(), found.begin(), found.end());
    }
    return result;
}
Snapshot snapshot(const Document &doc, Id terrain) {
    Snapshot s;
    s.tiles = tilesOf(doc, terrain);
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    const auto world = doc.worldTransform(terrain);
    for (const auto &[id, tile] : s.tiles)
        for (const auto &[vid, p] : tile->surface.vertices) {
            const auto m = world.point(p);
            minX = std::min(minX, m.x);
            minY = std::min(minY, m.y);
            maxX = std::max(maxX, m.x);
            maxY = std::max(maxY, m.y);
        }
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < 40; ++j)
            s.probes.push_back({minX + (maxX - minX) * (i + .37) / 40,
                                minY + (maxY - minY) * (j + .61) / 40});
    s.elevations = elevationsAt(doc, terrain, s.probes);
    return s;
}
std::size_t checkC5(const Document &doc, Id terrain, const Snapshot &before,
                    const std::string &when) {
    const auto after = tilesOf(doc, terrain);
    std::vector<BodyPtr> replaced;
    for (const auto &[id, tile] : before.tiles) {
        const auto found = after.find(id);
        if (found == after.end())
            replaced.push_back(tile);
        else
            check(found->second == tile, when + ": C5, a kept tile is pointer-identical");
    }
    check(replaced.size() < before.tiles.size(), when + ": C5, the edit is local");
    const auto world = doc.worldTransform(terrain);
    auto inReplaced = [&](const std::array<double, 2> &p) {
        for (const auto &tile : replaced) {
            double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            for (const auto &[vid, v] : tile->surface.vertices) {
                const auto m = world.point(v);
                minX = std::min(minX, m.x);
                minY = std::min(minY, m.y);
                maxX = std::max(maxX, m.x);
                maxY = std::max(maxY, m.y);
            }
            if (p[0] >= minX - 1e-6 && p[0] <= maxX + 1e-6 && p[1] >= minY - 1e-6 &&
                p[1] <= maxY + 1e-6)
                return true;
        }
        return false;
    };
    const auto now = elevationsAt(doc, terrain, before.probes);
    std::size_t compared = 0;
    for (std::size_t i = 0; i < before.probes.size(); ++i) {
        if (inReplaced(before.probes[i]))
            continue;
        ++compared;
        check(now[i].found == before.elevations[i].found &&
                  (!now[i].found || now[i].elevation == before.elevations[i].elevation),
              when + ": C5, elevations outside the edited tiles are bit-equal");
    }
    check(compared > before.probes.size() / 2, when + ": C5 compared most probes");
    return replaced.size();
}
void checkAll(Document &doc, Id terrain, TerrainContourCache &cache, const std::string &when) {
    checkC1(doc, terrain, when);
    checkC2(doc, terrain, cache, when);
    checkC6(doc, terrain, when);
    checkC7(doc, terrain, when);
}
// Undo restores the record and tiles and the encoding (but the ID allocator); Redo the edit; save
// and reopen keeps it bit for bit and C1, C6 and C7 hold there.
void checkHistoryAndSave(Document &doc, Id terrain, const QByteArray &before,
                         const std::string &when) {
    const auto after = encoded(doc);
    const auto tiles = tilesOf(doc, terrain);
    doc.undo();
    check(model(doc) == before, when + ": Undo restores the encoding");
    doc.redo();
    check(encoded(doc) == after, when + ": Redo restores the edit");
    const auto redone = tilesOf(doc, terrain);
    check(redone == tiles, when + ": Redo restores the same tile records");
    auto reopened = decodeDocument(encodeDocument(doc));
    check(encodeDocument(reopened) == encodeDocument(doc), when + ": save and reopen bit for bit");
    checkC1(reopened, terrain, when + " (reopened)");
    checkC6(reopened, terrain, when + " (reopened)");
    checkC7(reopened, terrain, when + " (reopened)");
}

// ---- Tests -----------------------------------------------------------------------------------
void baseConsistency() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    {
        auto display = recordOf(doc, terrain).display;
        display.contourInterval = 0.5;
        display.contours = true;
        setTerrainDisplay(doc, terrain, display);
    }
    checkC1(doc, terrain, "imported");
    checkC2(doc, terrain, cache, "imported");
    {
        auto reopened = decodeDocument(encodeDocument(doc));
        checkC7(reopened, terrain, "imported (reopened)");
        QJsonObject probe{{"z", -0.0}};
        const auto text = QJsonDocument(probe).toJson(QJsonDocument::Compact);
        const auto back = QJsonDocument::fromJson(text).object()["z"].toDouble();
        std::cout << "json -0.0: " << text.toStdString() << " back signbit " << std::signbit(back) << "\n";
    }
    const auto description = terrainDescription(doc, terrain);
    const auto current = description["currentSources"].toObject();
    check(current["count"] == 40 && current["roles"].toObject()["contour"] == 40,
          "terrain.describe reports the current sources");
    std::cout << "base: " << terrainTriangleCount(doc, terrain) << " triangles, C1 and C2 hold\n";
}

// Moving one contour vertex outward: a small cavity; C1, C2, C5, C6, C7; Undo, Redo, reopen.
void moveVertex() {
    Document doc;
    const auto terrain = importRings(doc);
    {
        auto display = recordOf(doc, terrain).display;
        display.contourInterval = 0.5;
        setTerrainDisplay(doc, terrain, display);
    }
    TerrainContourCache cache;
    (void)cache.get(doc, terrain);
    const auto ring = contours(*stateOf(doc, terrain)).at(102.5);
    auto points = ring->points;
    // Move vertex 7 (and so not the closing pair) a third of the way to the next ring.
    const double scale = 1.0 + 1.0 / (10 + 3 * 5) * 0.9;
    points[7] = radial(points[7], centre(doc, terrain), scale);
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    // The preview equals the commit bit for bit.
    auto data = std::make_shared<TerrainSourceEditData>();
    data->source = ring->id;
    data->role = TerrainSourceRole::Contour;
    data->points = points;
    data->before = {-1e5, -1e5, 1e5, 1e5};
    const auto preview = previewTerrainEdit(doc, terrain, terrainEditSourceKind, data);
    check(preview.removedTriangles > 0 && preview.removedTriangles < 60,
          "moving one vertex re-triangulates a small cavity");
    const auto operation = run(doc, command("terrain.edit_source", terrain,
                                            {{"source", double(ring->id)},
                                             {"points", modelPoints(doc, terrain, points)}}));
    check(operation["edit"] == 1 && operation["source"] == double(ring->id),
          "the edit names its entry and source");
    const auto &record = recordOf(doc, terrain);
    check(record.edits.size() == 1 && record.edits[0]->kind == terrainEditSourceKind,
          "the edit is recorded as editSource");
    std::map<TerrainTileKey, TerrainTileContent> committed;
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        committed[tile->tileKey] = *terrainTileContent(*tile);
    for (const auto &tile : preview.region.tiles)
        check(committed.contains(tile->key) && committed.at(tile->key).sameMesh(*tile),
              "the committed tiles equal the previewed candidate");
    const auto moved = stateOf(doc, terrain)->find(ring->id);
    check(moved && moved->points == points, "the current source holds the moved vertex");
    const auto replaced = checkC5(doc, terrain, snap, "move vertex");
    checkAll(doc, terrain, cache, "move vertex");
    checkHistoryAndSave(doc, terrain, before, "move vertex");
    // Determinism: the same edit on a second document gives an identical document.
    Document other;
    const auto copy = importRings(other);
    {
        auto display = recordOf(other, copy).display;
        display.contourInterval = 0.5;
        setTerrainDisplay(other, copy, display);
    }
    run(other, command("terrain.edit_source", copy,
                       {{"source", double(ring->id)},
                        {"points", modelPoints(other, copy, points)}}));
    check(model(other) == model(doc), "the same edit gives identical documents");
    std::cout << "move vertex: cavity " << preview.removedTriangles << " -> "
              << preview.addedTriangles << " triangles, " << replaced << " tiles replaced\n";
}

// The elevation-only path: a ring raised between its neighbours (lifts only), then raised to its
// inner neighbour's elevation (flat triangles appear and are flipped where they can be).
void elevationOnly() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    {
        auto display = recordOf(doc, terrain).display;
        display.contourInterval = 0.25;
        setTerrainDisplay(doc, terrain, display);
    }
    (void)cache.get(doc, terrain);
    const auto state = stateOf(doc, terrain);
    const auto ring = contours(*state).at(102.0);
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    TerrainSource raised = *ring;
    for (auto &p : raised.points)
        p.z = 102.25;
    const auto plan =
        planTerrainSourceEdit({*mesh, *state, recordOf(doc, terrain).parameters}, ring.get(), &raised);
    check(plan.elevationOnly && plan.change.removed.empty() && plan.change.added.empty() &&
              plan.change.lifted.size() == 160,
          "an elevation change between the neighbours lifts the 160 vertices only");
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const auto elevation = toModel(doc, terrain, {0, 0, 102.25}).z;
    run(doc, command("terrain.edit_source", terrain,
                     {{"source", double(ring->id)}, {"elevation", elevation}}));
    check(stateOf(doc, terrain)->find(ring->id)->points.front().z == 102.25,
          "the contour takes its new elevation");
    checkC5(doc, terrain, snap, "elevation");
    checkAll(doc, terrain, cache, "elevation");
    checkHistoryAndSave(doc, terrain, before, "elevation");
    // Up to the inner neighbour's 102.5: flats between the two rings are flipped where a flip
    // keeps the constraints.
    TerrainSource flat = *stateOf(doc, terrain)->find(ring->id);
    for (auto &p : flat.points)
        p.z = 102.5;
    const auto flatPlan = planTerrainSourceEdit(
        {*terrainEditMeshCache().get(doc, terrain, true), *stateOf(doc, terrain),
         recordOf(doc, terrain).parameters},
        stateOf(doc, terrain)->find(ring->id), &flat);
    check(flatPlan.elevationOnly, "the flat case is elevation-only too");
    run(doc, command("terrain.edit_source", terrain,
                     {{"source", double(ring->id)},
                      {"elevation", toModel(doc, terrain, {0, 0, 102.5}).z}}));
    checkAll(doc, terrain, cache, "elevation to a neighbour");
    // Contour to breakline and back: the role is recorded; the mesh follows the flat rule.
    run(doc, command("terrain.edit_source", terrain,
                     {{"source", double(ring->id)}, {"role", "breakline"}}));
    check(stateOf(doc, terrain)->find(ring->id)->role == TerrainSourceRole::Breakline,
          "the role changes to breakline");
    checkC1(doc, terrain, "role");
    checkC6(doc, terrain, "role");
    checkC7(doc, terrain, "role");
    std::cout << "elevation-only: 160 lifts; to the neighbour " << flatPlan.flips << " flips, "
              << flatPlan.flatRemaining << " flat remain\n";
}

// Add a contour, a breakline and spot points, remove a source; promotion of a display contour.
void addAndRemove() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    {
        auto display = recordOf(doc, terrain).display;
        display.contourInterval = 0.25;
        setTerrainDisplay(doc, terrain, display);
    }
    (void)cache.get(doc, terrain);
    const auto maxBase = stateOf(doc, terrain)->maxBaseId();
    // A closed contour at 103.25 between the rings at 103.0 (r = 16) and 103.5 (r = 13).
    std::vector<Vec3> ring;
    for (int i = 0; i < 24; ++i) {
        const double t = 2 * std::numbers::pi * i / 24 + 0.3;
        ring.push_back({14.5 * std::cos(t) * 1.3, 14.5 * std::sin(t), 103.25});
    }
    ring = rel(doc, terrain, ring);
    ring.push_back(ring.front());
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const auto added = run(doc, command("terrain.add_source", terrain,
                                        {{"role", "contour"},
                                         {"points", modelPoints(doc, terrain, ring, false)},
                                         {"elevation", toModel(doc, terrain, {0, 0, 103.25}).z}}));
    const auto id = std::uint32_t(added["source"].toDouble());
    check(id == maxBase + 1, "an added source takes the id above the base ids and its entry");
    const auto *source = stateOf(doc, terrain)->find(id);
    check(source && source->role == TerrainSourceRole::Contour &&
              source->reference == "edit 1" && source->points.size() == 25,
          "the added contour is a current source");
    checkC5(doc, terrain, snap, "add contour");
    checkAll(doc, terrain, cache, "add contour");
    checkHistoryAndSave(doc, terrain, before, "add contour");
    // A breakline across the gap between two rings, and two spot points.
    const auto b1 = run(doc, command("terrain.add_source", terrain,
                                     {{"role", "breakline"},
                                      {"points", modelPoints(doc, terrain,
                                                             rel(doc, terrain, {{20.0 * 1.3 * .99, 1.0, 102.6},
                                                              {21.5 * 1.3 * .99, 2.0, 102.2}}))}}));
    const auto p1 = run(doc, command("terrain.add_source", terrain,
                                     {{"role", "points"},
                                      {"points", modelPoints(doc, terrain,
                                                             rel(doc, terrain, {{0, 25.5, 101.75}, {0, -25.5, 101.7}}))}}));
    check(b1["source"] == double(maxBase + 2) && p1["source"] == double(maxBase + 3),
          "each added source takes its entry's id above the base");
    checkC1(doc, terrain, "breakline and points");
    checkC6(doc, terrain, "breakline and points");
    checkC7(doc, terrain, "breakline and points");
    // Remove the added contour, then a base ring.
    const auto removing = model(doc);
    run(doc, command("terrain.remove_source", terrain, {{"source", double(id)}}));
    check(!stateOf(doc, terrain)->find(id), "the removed source is gone");
    const auto ring4 = contours(*stateOf(doc, terrain)).at(103.0)->id;
    run(doc, command("terrain.remove_source", terrain, {{"source", double(ring4)}}));
    checkAll(doc, terrain, cache, "remove");
    doc.undo();
    doc.undo();
    check(model(doc) == removing, "two undos restore the removed sources");
    doc.redo();
    doc.redo();
    // Promotion: the display contour at 101.25 (between the rings at 101.5 and 101.0).
    const auto promoted =
        run(doc, command("terrain.add_source", terrain,
                         {{"level", toModel(doc, terrain, {0, 0, 101.25}).z}}));
    check(promoted["terrainReport"].toObject()["terrain"].toObject()["promoted"] == true,
          "the report names the promotion");
    const auto *line = stateOf(doc, terrain)->find(std::uint32_t(promoted["source"].toDouble()));
    check(line && line->role == TerrainSourceRole::Contour && line->points.front().z == 101.25 &&
              line->points.front() == line->points.back(),
          "the promoted display contour is a closed source contour at its level");
    checkAll(doc, terrain, cache, "promote");
    // Every change undoes to the imported document.
    while (!recordOf(doc, terrain).edits.empty())
        doc.undo();
    check(model(doc) == before, "undoing every source edit restores the import");
    std::cout << "add and remove: " << line->points.size() << "-point promoted contour\n";
}

// Refusals: located, in the import's report kinds, and nothing changes.
void refusals() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto state = stateOf(doc, terrain);
    const auto rings = contours(*state);
    const auto ring = rings.at(103.0);
    const auto before = encoded(doc);
    const auto revision = doc.revision();
    auto unchanged = [&](const std::string &what) {
        check(encoded(doc) == before && doc.revision() == revision,
              what + " changes nothing");
    };
    auto kinds = [](const Refusal &r) {
        std::set<std::string> result;
        for (const auto &value : r.report["terrain"].toObject()["refusals"].toArray())
            result.insert(value.toObject()["kind"].toString().toStdString());
        return result;
    };
    // A new contour crossing the 103.0 ring.
    {
        const auto r = refusal([&] {
            direct(doc, command("terrain.add_source", terrain,
                                {{"role", "contour"},
                                 {"points", modelPoints(doc, terrain,
                                                        rel(doc, terrain, {{0, 13, 103.2}, {0, 19, 103.2}}))}}));
        });
        check(r.code == "INVALID_REQUEST" && kinds(r).contains("crossing-constraints"),
              "a crossing contour is refused as crossing-constraints: " + r.message);
        const auto located = r.report["terrain"].toObject()["refusals"].toArray().at(0).toObject();
        check(!located["locations"].toArray().isEmpty() &&
                  located["locations"].toArray().at(0).toObject().contains("xy"),
              "the crossing is located");
        unchanged("a crossing contour");
    }
    // Moving a vertex across the neighbouring ring.
    {
        auto points = ring->points;
        points[3] = radial(points[3], centre(doc, terrain), 1.5);
        const auto r = refusal([&] {
            direct(doc, command("terrain.edit_source", terrain,
                                {{"source", double(ring->id)},
                                 {"points", modelPoints(doc, terrain, points)}}));
        });
        check(kinds(r).contains("crossing-constraints"), "a vertex moved across a ring is refused");
        unchanged("a vertex moved across a ring");
    }
    // Outside the outline.
    {
        const auto r = refusal([&] {
            direct(doc, command("terrain.add_source", terrain,
                                {{"role", "points"},
                                 {"points", modelPoints(doc, terrain, rel(doc, terrain, {{200, 200, 90}}))}}));
        });
        check(kinds(r).contains("outside-outline"), "a point outside the outline is refused");
        unchanged("a point outside the outline");
        const auto s = refusal([&] {
            direct(doc, command("terrain.add_source", terrain,
                                {{"role", "breakline"},
                                 {"points", modelPoints(doc, terrain,
                                                        rel(doc, terrain, {{0, 125.5, 86}, {0, 200, 86}}))}}));
        });
        check(kinds(s).contains("outside-outline"), "a breakline leaving the outline is refused");
        unchanged("a breakline leaving the outline");
    }
    // A conflicting duplicate: a spot point on a contour vertex at another elevation.
    {
        auto p = ring->points[5];
        p.z += 1;
        const auto r = refusal([&] {
            direct(doc, command("terrain.add_source", terrain,
                                {{"role", "points"}, {"points", modelPoints(doc, terrain, {p})}}));
        });
        check(kinds(r).contains("conflicting-duplicate"), "a conflicting duplicate is refused");
        unchanged("a conflicting duplicate");
    }
    // Requests.
    check(refusal([&] {
              direct(doc, command("terrain.remove_source", terrain, {{"source", 999}}));
          }).code == "NOT_FOUND",
          "an unknown source is NOT_FOUND");
    check(refusal([&] {
              direct(doc, command("terrain.edit_source", terrain, {{"source", double(ring->id)}}));
          }).code == "INVALID_REQUEST",
          "an edit with nothing to change is refused");
    check(refusal([&] {
              direct(doc, command("terrain.add_source", terrain,
                                  {{"role", "contour"},
                                   {"points", modelPoints(doc, terrain, rel(doc, terrain, {{0, 14, 103.1},
                                                                         {1, 14, 103.2}}))}}));
          }).code == "INVALID_REQUEST",
          "a contour at two elevations is refused");
    check(refusal([&] {
              direct(doc, command("terrain.add_source", terrain, {{"level", 500.0}}));
          }).code == "NOT_FOUND",
          "no display contour at a level is NOT_FOUND");
    {
        auto body = std::make_shared<Body>(*doc.bodies().at(terrain));
        body->locked = true;
        doc.apply({"Lock", {{terrain, doc.bodies().at(terrain), body}}}, doc.revision());
        check(refusal([&] {
                  direct(doc, command("terrain.remove_source", terrain,
                                      {{"source", double(ring->id)}}));
              }).code == "INVALID_OPERATION",
              "a locked terrain is refused");
        doc.undo();
    }
    std::cout << "refusals: crossing, outline, duplicate, requests and lock refused, nothing "
                 "changed\n";
}

// The R095 contour fixture: boundary, pond hole and clipped hairpins. Edits into the pond are
// refused; a contour edit keeps C1, C6 and C7.
void fixtureOutlineAndHole() {
    Document doc;
    const QJsonObject options{{"format", "dxf"},
                              {"roles", QJsonObject{{"BOUNDARY", "boundary"}, {"POND", "hole"}}}};
    const auto imported = importTerrainSource(doc, fixture::terrainContoursMetres(), options);
    const Id terrain = imported["terrain"].toString().toULongLong();
    const auto state = stateOf(doc, terrain);
    const TerrainSource *pond = nullptr, *contour = nullptr;
    for (const auto *source : state->sources()) {
        if (source->role == TerrainSourceRole::Hole)
            pond = source;
        if (source->role == TerrainSourceRole::Contour && !contour && source->points.size() > 8)
            contour = source;
    }
    check(pond && contour, "the fixture has a pond and contours");
    double cx = 0, cy = 0;
    for (const auto &p : pond->points) {
        cx += p.x;
        cy += p.y;
    }
    cx /= double(pond->points.size());
    cy /= double(pond->points.size());
    const auto r = refusal([&] {
        direct(doc, command("terrain.add_source", terrain,
                            {{"role", "points"},
                             {"points", modelPoints(doc, terrain, {{cx, cy, 99}})}}));
    });
    check(r.code == "INVALID_REQUEST" && r.message.find("outside-outline") != std::string::npos,
          "a point in the pond is outside the outline: " + r.message);
    check(refusal([&] {
              direct(doc, command("terrain.remove_source", terrain, {{"source", double(pond->id)}}));
          }).code == "INVALID_REQUEST",
          "the pond (a hole source) is not removed by a source edit");
    // A breakline between two vertices of the pond's shore, across the pond: no outline edge is
    // crossed, but its middle is in the hole.
    {
        std::vector<Vec3> shore;
        for (const auto id : terrainTiles(doc, terrain))
            for (const auto &[vid, p] : doc.bodies().at(id)->surface.vertices)
                for (const auto &q : pond->points)
                    if (p.x == q.x && p.y == q.y &&
                        std::none_of(shore.begin(), shore.end(), [&](const Vec3 &s) {
                            return s.x == p.x && s.y == p.y;
                        }))
                        shore.push_back(p);
        check(shore.size() >= 4, "the pond's vertices are mesh vertices");
        const auto across = refusal([&] {
            direct(doc, command("terrain.add_source", terrain,
                                {{"role", "breakline"},
                                 {"points", modelPoints(doc, terrain,
                                                        {shore[0], shore[shore.size() / 2]})}}));
        });
        check(across.message.find("outside-outline") != std::string::npos,
              "a breakline across the pond between two shore vertices is refused: " +
                  across.message);
    }
    checkC6(doc, terrain, "fixture");
    std::cout << "fixture: a point in the pond is refused\n";
}

// Points terrain: add and remove spot points far from most tiles; replay after remove_edit and
// reset; stable ids across Reset edits.
void pointsTerrain() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto snap = snapshot(doc, terrain);
    const auto before = model(doc);
    const auto added = run(doc, command("terrain.add_source", terrain,
                                        {{"role", "points"},
                                         {"points", modelPoints(doc, terrain,
                                                                rel(doc, terrain, {{0.5, 0.25, 125.0},
                                                                 {1.5, 0.75, 125.5}}))}}));
    const auto first = std::uint32_t(added["source"].toDouble());
    checkC5(doc, terrain, snap, "spot points");
    checkC6(doc, terrain, "spot points");
    checkC7(doc, terrain, "spot points");
    checkHistoryAndSave(doc, terrain, before, "spot points");
    const auto second = std::uint32_t(
        run(doc, command("terrain.add_source", terrain,
                         {{"role", "breakline"},
                          {"points", modelPoints(doc, terrain,
                                                 rel(doc, terrain, {{-99.75, -49.75, 121.0}, {-89.75, -45.75, 122.0}}))}}))
            ["source"]
                .toDouble());
    check(second == first + 1, "ids follow the entries");
    // remove_edit of the first entry replays the breakline alone; reset clears both; a later add
    // never reuses an id.
    run(doc, command("terrain.remove_edit", terrain, {{"edit", 1}}));
    check(!stateOf(doc, terrain)->find(first) && stateOf(doc, terrain)->find(second),
          "removing a change removes its source and keeps the others");
    checkC6(doc, terrain, "remove_edit");
    checkC7(doc, terrain, "remove_edit");
    run(doc, command("terrain.reset_edits", terrain));
    check(stateOf(doc, terrain)->size() == recordOf(doc, terrain).sources.size(),
          "reset restores the base sources");
    const auto third = std::uint32_t(
        run(doc, command("terrain.add_source", terrain,
                         {{"role", "points"},
                          {"points", modelPoints(doc, terrain, rel(doc, terrain, {{-149.5, -89.5, 119.0}}))}}))["source"]
            .toDouble());
    check(third > second, "a source id is never reused, even after Reset edits");
    checkC7(doc, terrain, "after reset");
    std::cout << "points terrain: ids " << first << ", " << second << ", " << third << "\n";
}

// A gridded spot-point terrain (a 1 m grid, co-circular everywhere, ADR 0169 §9): a breakline,
// a spot point and a removal keep distant terrain (C5), seams (C6) and Regenerate (C7), and the
// same edits on a second document give identical tiles despite the ties.
QByteArray gridSource(int n) {
    QByteArray file;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            file += QByteArray::number(i) + ' ' + QByteArray::number(j) + ' ' +
                    QByteArray::number(50 + 0.01 * i + 0.02 * j, 'f', 3) + '\n';
    return file;
}
Id importGrid(Document &doc, int n = 120) {
    const QJsonObject options{{"format", "points"}, {"units", "m"}, {"columns", "XYZ"},
                              {"delimiter", "whitespace"}, {"header", false}};
    return importTerrainSource(doc, gridSource(n), options)["terrain"].toString().toULongLong();
}
void griddedTerrain() {
    auto edit = [](Document &doc, Id terrain) {
        run(doc, command("terrain.add_source", terrain,
                         {{"role", "breakline"},
                          {"points", modelPoints(doc, terrain,
                                                 rel(doc, terrain, {{-20.5, -10.25, 51.5},
                                                                    {-5.25, 3.5, 51.2},
                                                                    {10.75, 4.25, 51.0}}))}}));
        run(doc, command("terrain.add_source", terrain,
                         {{"role", "points"},
                          {"points", modelPoints(doc, terrain, rel(doc, terrain, {{30.25, -30.75, 49}}))}}));
    };
    Document doc;
    const auto terrain = importGrid(doc);
    const auto snap = snapshot(doc, terrain);
    const auto before = model(doc);
    edit(doc, terrain);
    const auto replaced = checkC5(doc, terrain, snap, "grid");
    checkC1(doc, terrain, "grid");
    checkC6(doc, terrain, "grid");
    checkC7(doc, terrain, "grid");
    auto reopened = decodeDocument(encodeDocument(doc));
    checkC7(reopened, terrain, "grid (reopened)");
    Document other;
    const auto copy = importGrid(other);
    edit(other, copy);
    check(model(other) == model(doc), "the same edits on a co-circular grid give identical "
                                      "documents");
    doc.undo();
    doc.undo();
    check(model(doc) == before, "two undos restore the grid");
    std::cout << "grid: " << terrainTriangleCount(other, copy) << " triangles, " << replaced
              << " tiles replaced\n";
}

// The pad rule: a test-only kind that protects a polygon stands in for R096.d's stamp.
struct Protect final : TerrainEditData {
    TerrainPlanPolygon polygon;
    bool equals(const TerrainEditData &other) const override {
        const auto *o = dynamic_cast<const Protect *>(&other);
        return o && o->polygon == polygon;
    }
    std::size_t bytes() const override { return sizeof(Protect) + polygon.size() * 16; }
};
void padRule() {
    registerTerrainEditKind(
        {"test.protect",
         [](const TerrainEditMesh &, const TerrainEdit &) {
             return TerrainEditOutcome{TerrainRegionChange{}, {}};
         },
         [](const TerrainEdit &) { return std::array<double, 4>{0, 0, 0, 0}; }, nullptr, nullptr,
         [](const TerrainEdit &edit) {
             return std::vector<TerrainPlanPolygon>{dynamic_cast<const Protect &>(*edit.data).polygon};
         }});
    Document doc;
    const auto terrain = importRings(doc);
    auto pad = std::make_shared<Protect>();
    const auto c = centre(doc, terrain);
    pad->polygon = {{c.x - 4, c.y - 4}, {c.x + 4, c.y - 4}, {c.x + 4, c.y + 4}, {c.x - 4, c.y + 4}};
    applyTerrainEdit(doc, terrain, "test.protect", pad, "Protect");
    const auto revision = doc.revision();
    const auto r = refusal([&] {
        direct(doc, command("terrain.add_source", terrain,
                            {{"role", "points"},
                             {"points", modelPoints(doc, terrain, rel(doc, terrain, {{1, 1, 105.5}}))}}));
    });
    check(r.message.find("inside-pad") != std::string::npos, "a point inside a pad is refused");
    const auto s = refusal([&] {
        direct(doc, command("terrain.add_source", terrain,
                            {{"role", "breakline"},
                             {"points", modelPoints(doc, terrain,
                                                    rel(doc, terrain, {{-6, 0, 105.1}, {6, 0.5, 105.1}}))}}));
    });
    check(s.message.find("inside-pad") != std::string::npos,
          "a breakline through a pad is refused");
    check(doc.revision() == revision, "the pad refusals change nothing");
    // Outside the pad the edit applies.
    run(doc, command("terrain.add_source", terrain,
                     {{"role", "points"}, {"points", modelPoints(doc, terrain, rel(doc, terrain, {{7, 0, 105.2}}))}}));
    std::cout << "pad rule: refused inside, applied outside\n";
}

constexpr const char *sequenceDigest =
    "42754c782da8528ae207045eec9b98b27e6bdfc3857b917e5be73afabd10cf1e";
// A scripted sequence of every kind, then Regenerate, Undo all, reopen.
void sequence() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    {
        auto display = recordOf(doc, terrain).display;
        display.contourInterval = 0.25;
        setTerrainDisplay(doc, terrain, display);
    }
    const auto start = model(doc);
    const auto rings = contours(*stateOf(doc, terrain));
    const auto r5 = rings.at(102.5), r7 = rings.at(101.5);
    for (int step = 0; step < 6; ++step) {
        auto points = stateOf(doc, terrain)->find(r5->id)->points;
        const auto i = std::size_t(5 + 6 * step);
        points[i] = radial(points[i], centre(doc, terrain), 1.02);
        if (i == 0)
            points.back() = points.front();
        run(doc, command("terrain.edit_source", terrain,
                         {{"source", double(r5->id)}, {"points", modelPoints(doc, terrain, points)}}));
    }
    run(doc, command("terrain.edit_source", terrain,
                     {{"source", double(r7->id)},
                      {"elevation", toModel(doc, terrain, {0, 0, 101.4}).z}}));
    run(doc, command("terrain.add_source", terrain,
                     {{"level", toModel(doc, terrain, {0, 0, 103.75}).z}}));
    run(doc, command("terrain.remove_source", terrain, {{"source", double(rings.at(100.5)->id)}}));
    checkAll(doc, terrain, cache, "sequence");
    // The replay digest of this fixture and sequence, pinned (ADR 0169 §9, determinism): a
    // change to the cavity, the flip or the tiling that alters replay output must update it
    // and bump the generator's edit part.
    const auto digest = recordOf(doc, terrain).meshDigest;
    std::cout << "sequence digest " << digest << "\n";
    check(digest == sequenceDigest, "the sequence's mesh digest is pinned: " + digest);
    auto reopened = decodeDocument(encodeDocument(doc));
    checkC7(reopened, terrain, "sequence (reopened)");
    std::size_t undone = 0;
    while (doc.canUndo() && !recordOf(doc, terrain).edits.empty()) {
        doc.undo();
        ++undone;
    }
    check(model(doc) == start, "undoing the sequence restores the original encoding");
    std::cout << "sequence: " << undone << " edits undone to the original\n";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;
    const std::vector<std::pair<const char *, void (*)()>> tests{
        {"base consistency", baseConsistency}, {"move vertex", moveVertex},
        {"elevation only", elevationOnly},     {"add and remove", addAndRemove},
        {"refusals", refusals},                {"fixture outline and hole", fixtureOutlineAndHole},
        {"points terrain", pointsTerrain},     {"gridded terrain", griddedTerrain},
        {"pad rule", padRule},
        {"sequence", sequence}};
    int failed = 0;
    for (const auto &[name, test] : tests) {
        const auto start = std::chrono::steady_clock::now();
        try {
            test();
            std::cout << "PASS " << name << " ("
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                             .count()
                      << " s)\n";
        } catch (const std::exception &error) {
            ++failed;
            std::cout << "FAIL " << name << ": " << error.what() << "\n";
        }
    }
    return failed ? 1 : 0;
}
