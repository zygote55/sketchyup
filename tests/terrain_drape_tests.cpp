// R096.e (ADR 0169 §4.4, §6, §8, §9): projection and drape. terrain.drape as the edit kind
// `drape` (imprint: split-only, the surface's shape unchanged, check C4) and as overlay (ordinary
// geometry in a new group, the terrain record unchanged): lines crossing contours, a breakline,
// tile seams, the outline and a hole; face loops with region paint that survives later edits and
// Regenerate; lines along grid edges and through grid vertices; the pad rule (decision 6); the
// refusals that change nothing; the hostile-input bounds and cancellation; and checks C1, C2, C4,
// C5, C6 and C7 after the edits, after Undo and Redo, after save and reopen and after Regenerate.
// The helpers are R096.d's.
#include "automation/assistant.hpp"
#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
#include "core/terrain_drape.hpp"
#include "core/materials.hpp"
#include "core/groups.hpp"
#include <random>
#include "core/terrain_sources.hpp"
#include "io/document_io.hpp"
#include "io/terrain_io.hpp"
#include "terrain_edit_test_kinds.hpp"
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

template <class F> void checkUnchanged(Document &doc, const std::string &what, F operation,
                                       const char *code) {
    const auto before = encoded(doc);
    const auto revision = doc.revision();
    const bool undo = doc.canUndo(), redo = doc.canRedo();
    const auto r = refusal(operation);
    check(r.code == code, what + " is refused with " + code + " (got " + r.code + ": " +
                              r.message + ")");
    check(doc.revision() == revision && encoded(doc) == before && doc.canUndo() == undo &&
              doc.canRedo() == redo,
          what + ": the refusal changes nothing");
}

// The falloff edges on the 1 m grid: a vertex at exactly the radius or the width stays; one
// inside moves by amount * f(d / radius); a region includes its outline.
QByteArray gridSource(int n) {
    QByteArray file;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            file += QByteArray::number(i) + ' ' + QByteArray::number(j) + ' ' +
                    QByteArray::number(50 + 0.01 * i + 0.02 * j, 'f', 3) + '\n';
    return file;
}
Id importGrid(Document &doc, int n = 40) {
    const QJsonObject options{{"format", "points"}, {"units", "m"}, {"columns", "XYZ"},
                              {"delimiter", "whitespace"}, {"header", false}};
    return importTerrainSource(doc, gridSource(n), options)["terrain"].toString().toULongLong();
}

// ---- Stamp helpers ---------------------------------------------------------------------------
QJsonObject stampCommand(Id terrain, QJsonObject fields) {
    return command("terrain.stamp_pad", terrain, std::move(fields));
}
QJsonArray planPoints(const Document &doc, Id terrain, const std::vector<std::array<double, 2>> &local) {
    QJsonArray points;
    for (const auto &p : local) {
        const auto m = toModel(doc, terrain, {p[0], p[1], 0});
        points.append(QJsonArray{m.x, m.y});
    }
    return points;
}
// Plan points relative to the terrain's centre, in its frame.
std::vector<std::array<double, 2>> relPlan(const Document &doc, Id terrain,
                                           std::vector<std::array<double, 2>> points) {
    const auto c = centre(doc, terrain);
    for (auto &p : points)
        p = {c.x + p[0], c.y + p[1]};
    return points;
}
QJsonObject polygonFootprint(const Document &doc, Id terrain,
                             const std::vector<std::array<double, 2>> &local) {
    return {{"polygon", planPoints(doc, terrain, local)}};
}
std::vector<std::array<double, 2>> square(double cx, double cy, double half) {
    return {{cx - half, cy - half}, {cx + half, cy - half}, {cx + half, cy + half}, {cx - half, cy + half}};
}
const TerrainStampData &lastStamp(const Document &doc, Id terrain) {
    return dynamic_cast<const TerrainStampData &>(*recordOf(doc, terrain).edits.back()->data);
}
const TerrainStampData &stampOf(const Document &doc, Id terrain, std::uint32_t edit) {
    for (const auto &entry : recordOf(doc, terrain).edits)
        if (entry->id == edit)
            return dynamic_cast<const TerrainStampData &>(*entry->data);
    throw std::runtime_error("no such stamp");
}
// The ground (terrain frame) at a plan point.
double groundAt(const Document &doc, Id terrain, double x, double y) {
    const auto m = toModel(doc, terrain, {x, y, 0});
    const auto found = terrainElevationAt(doc, terrain, {{m.x, m.y}});
    check(found.front().found, "the point is on the terrain");
    return found.front().elevation - toModel(doc, terrain, {0, 0, 0}).z;
}
void checkC3(const Document &doc, Id terrain, const std::string &when,
             const std::vector<std::uint32_t> &skipped = {}) {
    const auto c3 = checkTerrainPadConsistency(doc, terrain, skipped);
    check(c3.ok() && c3.pads > 0 && c3.padVertices > 0,
          when + ": C3, pad heights hold (" +
              (c3.problems.empty() ? std::string("no pads") : c3.problems.front()) + ")");
    // terrain.elevation_at inside each pad's footprint returns its elevation within 1e-9 m.
    const auto world = doc.worldTransform(terrain);
    for (const auto &entry : recordOf(doc, terrain).edits) {
        if (entry->kind != terrainStampKind || std::find(skipped.begin(), skipped.end(), entry->id) != skipped.end())
            continue;
        const auto &data = dynamic_cast<const TerrainStampData &>(*entry->data);
        if (!data.resolved())
            continue;
        const TerrainPolygonIndex footprint(data.footprint);
        const auto &box = footprint.box();
        std::vector<std::array<double, 2>> probes;
        for (int i = 0; i < 12; ++i)
            for (int j = 0; j < 12; ++j) {
                const double x = box[0] + (box[2] - box[0]) * (i + 0.5) / 12,
                             y = box[1] + (box[3] - box[1]) * (j + 0.5) / 12;
                if (footprint.contains(x, y)) {
                    const auto m = world.point({x, y, 0});
                    probes.push_back({m.x, m.y});
                }
            }
        check(!probes.empty(), when + ": C3 probes inside the pad");
        const double h = world.point({0, 0, data.elevation}).z;
        for (const auto &found : elevationsAt(doc, terrain, probes))
            check(found.found && std::abs(found.elevation - h) <= 1e-9,
                  when + ": C3, elevation_at inside the pad is its elevation");
    }
}
template <class F> std::string refusalReportKinds(F operation) {
    const auto r = refusal(operation);
    std::string kinds;
    for (const auto &value : r.report["terrain"].toObject()["refusals"].toArray())
        kinds += value.toObject()["kind"].toString().toStdString() + " ";
    return r.code + ": " + kinds + "| " + r.message;
}


// ---- Drape helpers ---------------------------------------------------------------------------
QJsonObject drapeCommand(Id terrain, QJsonObject fields) {
    if (!fields.contains("mode"))
        fields["mode"] = "imprint";
    return command("terrain.drape", terrain, std::move(fields));
}
// A loose polyline of model edges (one geometry body), and its edges as {body, edge}.
Id addPolyline(Document &doc, const std::vector<Vec3> &model) {
    const Id body = doc.addWire(0, model[0], model[1]);
    for (std::size_t i = 1; i + 1 < model.size(); ++i)
        doc.addWire(body, model[i], model[i + 1]);
    return body;
}
QJsonArray edgesOf(const Document &doc, Id body) {
    QJsonArray edges;
    for (const auto &[edge, record] : doc.bodies().at(body)->topology.edges)
        edges.append(QJsonObject{{"body", QString::number(body)}, {"edge", QString::number(edge)}});
    return edges;
}
QJsonArray facesOf(const Document &doc, Id body) {
    QJsonArray faces;
    for (const auto &[face, record] : doc.bodies().at(body)->surface.faces)
        faces.append(QJsonObject{{"body", QString::number(body)}, {"face", QString::number(face)}});
    return faces;
}
// Model points from plan points in the terrain frame, at a height `z` above the frame's origin
// (drape ignores it).
std::vector<Vec3> modelLine(const Document &doc, Id terrain,
                            const std::vector<std::array<double, 2>> &local, double z = 5) {
    std::vector<Vec3> points;
    for (const auto &p : local)
        points.push_back(toModel(doc, terrain, {p[0], p[1], z}));
    return points;
}
const TerrainDrapeData &lastDrape(const Document &doc, Id terrain) {
    return dynamic_cast<const TerrainDrapeData &>(*recordOf(doc, terrain).edits.back()->data);
}
// C4: terrain.elevation_at at `count` random model points in the box is unchanged within 1e-9 m.
struct Probes {
    std::vector<std::array<double, 2>> points;
    std::vector<TerrainElevation> before;
};
Probes probesIn(const Document &doc, Id terrain, std::array<double, 4> local, std::size_t count,
                std::uint32_t seed = 20261020) {
    Probes probes;
    std::mt19937_64 random(seed);
    std::uniform_real_distribution<double> u(0, 1);
    for (std::size_t i = 0; i < count; ++i) {
        const auto m = toModel(doc, terrain,
                               {local[0] + (local[2] - local[0]) * u(random),
                                local[1] + (local[3] - local[1]) * u(random), 0});
        probes.points.push_back({m.x, m.y});
    }
    probes.before = elevationsAt(doc, terrain, probes.points);
    return probes;
}
void checkC4(const Document &doc, Id terrain, const Probes &probes, const std::string &when) {
    const auto after = elevationsAt(doc, terrain, probes.points);
    double worst = 0;
    std::size_t found = 0;
    for (std::size_t i = 0; i < after.size(); ++i) {
        check(after[i].found == probes.before[i].found, when + ": C4, the same points are on the terrain");
        if (!after[i].found)
            continue;
        ++found;
        worst = std::max(worst, std::abs(after[i].elevation - probes.before[i].elevation));
    }
    check(worst <= 1e-9, when + ": C4, imprint changes no elevation (worst " + std::to_string(worst) + " m)");
    check(found > probes.points.size() / 2, when + ": C4 compared most probes");
    std::cout << when << ": C4 at " << found << " points, worst " << worst << " m\n";
}
std::array<double, 4> boxOf(const std::vector<std::array<double, 2>> &points, double margin) {
    std::array<double, 4> box{1e300, 1e300, -1e300, -1e300};
    for (const auto &p : points) {
        box[0] = std::min(box[0], p[0] - margin);
        box[1] = std::min(box[1], p[1] - margin);
        box[2] = std::max(box[2], p[0] + margin);
        box[3] = std::max(box[3], p[1] + margin);
    }
    return box;
}
// Every run of the last drape is a current breakline whose segments are mesh edges (C1 covers it);
// run points on a current contour carry its exact elevation.
void checkRuns(const Document &doc, Id terrain, const std::string &when) {
    const auto &data = lastDrape(doc, terrain);
    const auto id = recordOf(doc, terrain).edits.back()->id;
    const auto state = stateOf(doc, terrain);
    check(data.resolved(), when + ": the drape applied");
    for (std::size_t i = 0; i < data.runs.size(); ++i) {
        const auto *source = state->find(*terrainSplitSourceId(id, std::uint32_t(i)));
        check(source && source->role == TerrainSourceRole::Breakline && source->points == data.runs[i],
              when + ": each run is a current breakline");
        check(state->isDrapeSource(source->id), when + ": the run is the drape's own");
    }
    std::map<TerrainPlanKey, double> contourZ;
    for (const auto *source : state->sources())
        if (source->role == TerrainSourceRole::Contour)
            for (const auto &p : source->points)
                contourZ[TerrainPlanKey::of(p.x, p.y)] = p.z;
    std::size_t onContour = 0;
    for (const auto &run : data.runs)
        for (const auto &p : run)
            if (const auto found = contourZ.find(TerrainPlanKey::of(p.x, p.y)); found != contourZ.end()) {
                check(p.z == found->second, when + ": a crossing of a contour takes its exact elevation");
                ++onContour;
            }
    std::cout << when << ": " << data.runs.size() << " runs, " << onContour << " points on contours, "
              << data.splits.size() << " source segments split\n";
}
std::map<TerrainTileKey, TerrainTilePtr> committedTiles(const Document &doc, Id terrain) {
    std::map<TerrainTileKey, TerrainTilePtr> committed;
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        committed[tile->tileKey] = terrainTileContent(*tile);
    return committed;
}
Id importFixture(Document &doc) {
    const QJsonObject options{{"format", "dxf"},
                              {"roles", QJsonObject{{"BOUNDARY", "boundary"}, {"POND", "hole"}}}};
    return importTerrainSource(doc, fixture::terrainContoursMetres(), options)["terrain"]
        .toString()
        .toULongLong();
}

// ---- Tests -----------------------------------------------------------------------------------
// A path across the hill: it crosses contours and tile seams; imprint keeps the shape (C4), the
// contours take the crossing points at their elevations, and every check holds.
void imprintPath() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    cache.get(doc, terrain);
    const auto c = centre(doc, terrain);
    const std::vector<std::array<double, 2>> path{
        {c.x - 70, c.y - 20}, {c.x - 30, c.y + 5}, {c.x + 10.37, c.y - 3.21}, {c.x + 66, c.y + 30}};
    const auto line = addPolyline(doc, modelLine(doc, terrain, path));
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const auto probes = probesIn(doc, terrain, boxOf(path, 3), 10000);
    // The preview equals the commit.
    TerrainDrapeData data;
    data.lines.push_back({path, false});
    const auto preview =
        previewTerrainEdit(doc, terrain, terrainDrapeKind, std::make_shared<TerrainDrapeData>(data));
    const auto result = run(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, line)}}));
    check(result["changed"].toBool() && result["mode"] == "imprint", "the drape imprints");
    const auto report = result["terrainReport"].toObject()["terrain"].toObject();
    std::cout << "path: " << report["runs"].toDouble() << " runs, " << report["points"].toDouble()
              << " points, " << report["crossings"].toDouble() << " crossings, "
              << report["trianglesSplit"].toDouble() << " -> " << report["trianglesAdded"].toDouble()
              << " triangles, " << report["tilesReplaced"].toDouble() << " tiles, "
              << report["sourcesSplit"].toDouble() << " source segments split\n";
    check(report["crossings"].toDouble() > 20 && report["sourcesSplit"].toDouble() > 10,
          "the path crosses mesh edges and splits contour segments");
    check(report["tilesReplaced"].toDouble() >= 2, "the path crosses tile seams");
    const auto committed = committedTiles(doc, terrain);
    for (const auto &tile : preview.region.tiles)
        check(committed.contains(tile->key) && committed.at(tile->key)->sameMesh(*tile),
              "the committed tiles equal the preview bit for bit");
    checkC4(doc, terrain, probes, "path");
    checkRuns(doc, terrain, "path");
    checkAll(doc, terrain, cache, "path");
    checkC5(doc, terrain, snap, "path");
    checkHistoryAndSave(doc, terrain, before, "path");
    // The same drape on a second document gives the identical terrain.
    Document second;
    const auto other = importRings(second);
    const auto otherLine = addPolyline(second, modelLine(second, other, path));
    run(second, drapeCommand(other, {{"edges", edgesOf(second, otherLine)}}));
    check(recordOf(second, other) == recordOf(doc, terrain) &&
              committedTiles(second, other).size() == committed.size(),
          "the same drape on another document is identical");
    for (const auto &[key, tile] : committedTiles(second, other))
        check(committed.at(key)->sameMesh(*tile), "the same drape gives the same tiles");
    // The imprinted line is the drape's own: source edits naming it are refused.
    const auto runId = *terrainSplitSourceId(recordOf(doc, terrain).edits.back()->id, 0);
    checkUnchanged(doc, "removing a draped line",
                   [&] { direct(doc, command("terrain.remove_source", terrain, {{"source", double(runId)}})); },
                   "INVALID_REQUEST");
    const auto r = refusalReportKinds(
        [&] { direct(doc, command("terrain.remove_source", terrain, {{"source", double(runId)}})); });
    check(r.find("drape-source") != std::string::npos, "the refusal names the draped line: " + r);
    // terrain.describe lists the drape entry.
    const auto description = terrainDescription(doc, terrain);
    check(description["edits"].toObject()["entries"].toArray().at(0).toObject()["kind"] == "drape",
          "terrain.describe lists the drape");
    std::cout << "path: C1, C2, C4, C5, C6, C7, history, save and determinism hold\n";
}

// The R095 contour fixture: a path from outside the boundary across the hill, the breakline and
// the pond (a hole) and out again. The parts outside are clipped and counted with a location; the
// breakline gains the crossing point.
void clipping() {
    Document doc;
    const auto terrain = importFixture(doc);
    TerrainContourCache cache;
    cache.get(doc, terrain);
    const std::vector<std::array<double, 2>> path{{-120, 1.3}, {30, 1.3}, {52.4, -5.1}, {120, -5.1}};
    const auto line = addPolyline(doc, modelLine(doc, terrain, path));
    const auto probes = probesIn(doc, terrain, {-96, -10, 96, 10}, 10000);
    const auto before = model(doc);
    const auto result = run(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, line)}}));
    const auto report = result["terrainReport"].toObject()["terrain"].toObject();
    std::cout << "clipping: " << report["clipped"].toDouble() << " parts clipped, "
              << report["runs"].toDouble() << " runs, " << report["sourcesSplit"].toDouble()
              << " source segments split\n";
    check(report["clipped"].toDouble() == 3, "three parts are clipped (both ends and the pond)");
    check(report["runs"].toDouble() == 2, "the path imprints as two runs (around the pond)");
    check(report.contains("clippedAt"), "the clipped part is located");
    bool notice = false;
    for (const auto &value : report["notices"].toArray())
        notice = notice || value.toObject()["kind"] == "drape-clipped";
    check(notice, "a drape-clipped notice is reported");
    // The breakline (x about 7) now holds the crossing point.
    const auto state = stateOf(doc, terrain);
    bool split = false;
    for (const auto *source : state->sources())
        if (source->role == TerrainSourceRole::Breakline && !state->isDrapeSource(source->id))
            for (const auto &p : source->points)
                split = split || std::abs(p.y - 1.3) < 1e-6;
    check(split, "the crossed breakline gains the crossing point");
    checkC4(doc, terrain, probes, "clipping");
    checkRuns(doc, terrain, "clipping");
    checkAll(doc, terrain, cache, "clipping");
    checkHistoryAndSave(doc, terrain, before, "clipping");
    // A line wholly outside the terrain or in the pond is refused, located, and changes nothing.
    const auto outside = addPolyline(doc, modelLine(doc, terrain, {{-150, 0}, {-120, 10}}));
    checkUnchanged(doc, "a line outside the terrain",
                   [&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, outside)}})); },
                   "INVALID_REQUEST");
    const auto pond = addPolyline(doc, modelLine(doc, terrain, {{51.5, -5.5}, {53.5, -4.5}}));
    const auto r = refusalReportKinds(
        [&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, pond)}})); });
    check(r.find("outside-outline") != std::string::npos, "a line in the pond is refused: " + r);
    std::cout << "clipping: outline and pond clipped, breakline split, refusals located\n";
}

// A painted face with a hole: imprint with region paint. Every triangle whose centroid is in the
// face takes its material, tiles inside but not crossed are repainted (same IDs), the paint
// survives a later sculpt and Regenerate, removing the drape removes it, and the material cannot
// be deleted while a region names it.
std::size_t paintedCount(const Document &doc, Id terrain, Id material) {
    std::size_t count = 0;
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        for (const auto &[face, sides] : tile->faceMaterials)
            count += sides.front == material && sides.back == material;
    return count;
}
void checkPaint(const Document &doc, Id terrain, Id material,
                const std::vector<std::array<double, 2>> &outer,
                const std::vector<std::array<double, 2>> &hole, const std::string &when) {
    const TerrainPolygonIndex outside(outer), inside(hole);
    std::size_t in = 0, out = 0;
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        for (const auto &[face, record] : tile->surface.faces) {
            const auto &loop = record.loops.front();
            const auto &a = tile->surface.vertices.at(loop[0]), &b = tile->surface.vertices.at(loop[1]),
                       &c = tile->surface.vertices.at(loop[2]);
            const double x = (a.x + b.x + c.x) / 3, y = (a.y + b.y + c.y) / 3;
            const bool expected = outside.contains(x, y) && !inside.contains(x, y);
            const auto found = tile->faceMaterials.find(face);
            const bool painted = found != tile->faceMaterials.end() &&
                                 found->second == MaterialSides{material, material};
            check(painted == expected, when + ": a triangle is painted exactly when its centroid is in the region");
            (expected ? in : out) += 1;
        }
    check(in > 0, when + ": some triangles are painted");
    std::cout << when << ": " << in << " triangles painted, " << out << " not\n";
}
void regionPaint() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    const std::vector<std::array<double, 2>> outer{{c.x - 150.3, c.y - 100.7}, {c.x + 160.1, c.y - 100.7},
                                                   {c.x + 160.1, c.y + 110.2}, {c.x - 150.3, c.y + 110.2}};
    const std::vector<std::array<double, 2>> hole{{c.x - 10.1, c.y - 5.3}, {c.x - 10.1, c.y + 12.9},
                                                  {c.x + 15.7, c.y + 12.9}, {c.x + 15.7, c.y - 5.3}};
    // A face with a hole, above the terrain, in a group (any context).
    const auto face = doc.addFace({modelLine(doc, terrain, outer, 40), modelLine(doc, terrain, hole, 40)}, "Path");
    const auto gravel = createMaterial(doc, "Gravel", {0.6f, 0.55f, 0.5f});
    assignMaterial(doc, face, doc.bodies().at(face)->surface.faces.begin()->first, gravel, true, false);
    createGroup(doc, {face}, "Paving");
    const auto before = model(doc);
    const auto tilesBefore = tilesOf(doc, terrain);
    const auto probes = probesIn(doc, terrain, boxOf(outer, 5), 10000);
    const auto result = run(doc, drapeCommand(terrain, {{"faces", facesOf(doc, face)}, {"paintRegions", true}}));
    const auto report = result["terrainReport"].toObject()["terrain"].toObject();
    std::cout << "paint: " << report["runs"].toDouble() << " runs, " << report["tilesReplaced"].toDouble()
              << " tiles replaced, " << report["tilesRepainted"].toDouble() << " repainted\n";
    check(report["paintRegions"].toDouble() == 1, "the face records one paint region");
    check(report["runs"].toDouble() == 2, "the face's two loops imprint as two closed runs");
    for (const auto &run : lastDrape(doc, terrain).runs)
        check(run.size() > 3 && run.front() == run.back(), "a face loop is a closed breakline");
    checkPaint(doc, terrain, gravel, outer, hole, "paint");
    check(report["tilesRepainted"].toDouble() > 0, "tiles inside the region but not crossed are repainted");
    checkC4(doc, terrain, probes, "paint");
    checkC1(doc, terrain, "paint");
    checkC6(doc, terrain, "paint");
    checkC7(doc, terrain, "paint");
    // Tiles neither replaced nor repainted are pointer-identical; repainted ones keep their mesh.
    {
        const auto removed = result["tiles"].toObject()["removed"].toArray();
        const auto repainted = result["tiles"].toObject()["repainted"].toArray();
        std::set<Id> changed;
        for (const auto &value : removed)
            changed.insert(value.toString().toULongLong());
        for (const auto &value : repainted) {
            const auto id = value.toString().toULongLong();
            changed.insert(id);
            check(doc.bodies().at(id)->surface == tilesBefore.at(id)->surface,
                  "a repainted tile keeps its mesh");
        }
        std::size_t kept = 0;
        for (const auto &[id, tile] : tilesBefore)
            if (!changed.contains(id)) {
                check(doc.bodies().at(id) == tile, "C5: an untouched tile is pointer-identical");
                ++kept;
            }
        check(kept > 0, "C5: some tiles are untouched");
    }
    checkHistoryAndSave(doc, terrain, before, "paint");
    // The paint survives a later sculpt over the region and Regenerate (C7, paint included).
    run(doc, command("terrain.sculpt", terrain,
                     {{"mode", "raise"}, {"path", planPoints(doc, terrain, {{c.x + 30, c.y - 20}, {c.x + 30, c.y + 30}})},
                      {"radius", 6}, {"amount", 0.5}}));
    checkPaint(doc, terrain, gravel, outer, hole, "paint after sculpt");
    checkC1(doc, terrain, "paint after sculpt");
    checkC6(doc, terrain, "paint after sculpt");
    checkC7(doc, terrain, "paint after sculpt");
    // The face paint is not "dropped" paint for Regenerate.
    check(terrainTilePaint(doc, terrain).faces == 0, "region paint is not counted as paint Regenerate drops");
    // terrain.describe lists the region.
    check(terrainDescription(doc, terrain)["paintRegions"].toObject()["count"].toDouble() == 1,
          "terrain.describe lists the paint region");
    // The material cannot be deleted while the region names it.
    checkUnchanged(doc, "deleting the region's material", [&] { eraseMaterial(doc, gravel); }, "other");
    // Removing the drape removes its paint and lines.
    const auto drapeId = recordOf(doc, terrain).edits.front()->id;
    run(doc, command("terrain.remove_edit", terrain, {{"edit", double(drapeId)}}));
    check(paintedCount(doc, terrain, gravel) == 0, "removing the drape removes its paint");
    checkC1(doc, terrain, "paint removed");
    checkC7(doc, terrain, "paint removed");
    std::cout << "paint: region paint follows edits, Regenerate and removal\n";
}

// Overlay: ordinary geometry in a new group in the active context, lifted, with one planar polygon
// per terrain-triangle piece of a face; the terrain record does not change; the faces count
// against the modelling limit.
void overlay() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    const std::vector<std::array<double, 2>> square{{c.x - 20.3, c.y - 10.7}, {c.x + 25.1, c.y - 10.7},
                                                    {c.x + 25.1, c.y + 15.2}, {c.x - 20.3, c.y + 15.2}};
    const auto face = doc.addFace({modelLine(doc, terrain, square, 30)}, "Lot");
    const auto line = addPolyline(doc, modelLine(doc, terrain, {{c.x - 60, c.y + 30}, {c.x + 70, c.y + 35}}));
    // The active context: a group elsewhere.
    const auto marker = doc.addWire(0, {0, 0, 0}, {1, 0, 0});
    const auto context = createGroup(doc, {marker}, "Site work");
    const auto recordBefore = recordOf(doc, terrain);
    const auto tilesBefore = tilesOf(doc, terrain);
    const auto facesBefore = terrainExplodeBudget(doc, terrain).modelFaces;
    const auto result = run(doc, drapeCommand(terrain, {{"mode", "overlay"},
                                                        {"faces", facesOf(doc, face)},
                                                        {"edges", edgesOf(doc, line)},
                                                        {"context", QString::number(context)},
                                                        {"lift", 0.05}}));
    check(result["mode"] == "overlay", "the drape overlays");
    check(recordOf(doc, terrain) == recordBefore && tilesOf(doc, terrain) == tilesBefore,
          "overlay changes neither the terrain record nor its tiles");
    const Id group = result["group"].toString().toULongLong(), body = result["body"].toString().toULongLong();
    check(doc.bodies().at(group)->kind == BodyKind::Group && doc.bodies().at(group)->parent == context &&
              doc.bodies().at(body)->parent == group,
          "the geometry is in a new group in the context");
    const auto &geometry = *doc.bodies().at(body);
    const auto faces = geometry.surface.faces.size();
    std::cout << "overlay: " << faces << " faces, " << geometry.surface.wires.size() << " loose edges, "
              << geometry.topology.edges.size() << " edges\n";
    check(faces > 20 && !geometry.surface.wires.empty(), "the overlay has faces and edges");
    check(terrainExplodeBudget(doc, terrain).modelFaces == facesBefore + faces,
          "the overlay's faces count against the modelling limit");
    // Every vertex lies 0.05 m above the surface; every face is planar.
    const auto world = doc.worldTransform(body);
    std::vector<std::array<double, 2>> points;
    std::vector<double> heights;
    for (const auto &[id, p] : geometry.surface.vertices) {
        const auto m = world.point(p);
        points.push_back({m.x, m.y});
        heights.push_back(m.z);
    }
    const auto ground = elevationsAt(doc, terrain, points);
    double worst = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        check(ground[i].found, "an overlay vertex lies over the terrain");
        worst = std::max(worst, std::abs(heights[i] - (ground[i].elevation + 0.05)));
    }
    check(worst <= 1e-9, "overlay vertices lie at the lift above the surface (worst " + std::to_string(worst) + ")");
    for (const auto &[id, record] : geometry.surface.faces) {
        const auto n = geometry.surface.normal(id);
        const auto &loop = record.loops.front();
        const auto o = geometry.surface.vertices.at(loop[0]);
        for (const auto v : loop)
            check(std::abs(dot(geometry.surface.vertices.at(v) - o, n)) < 1e-9, "an overlay face is planar");
    }
    // The faces cover the square: their plan area is its area.
    double area = 0;
    for (const auto &[id, record] : geometry.surface.faces) {
        for (std::size_t l = 0; l < record.loops.size(); ++l) {
            double a = 0;
            const auto &loop = record.loops[l];
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const auto &p = geometry.surface.vertices.at(loop[i]), &q = geometry.surface.vertices.at(loop[(i + 1) % loop.size()]);
                a += p.x * q.y - q.x * p.y;
            }
            area += a / 2;
        }
    }
    const double expected = (25.1 + 20.3) * (15.2 + 10.7);
    check(std::abs(area - expected) < 1e-6, "the overlay faces cover the face exactly (" + std::to_string(area) + ")");
    doc.undo();
    check(!doc.bodies().contains(group), "Undo removes the overlay");
    // A face limit below the overlay's count is refused (the core bound the command applies).
    {
        TerrainDrapeData data;
        data.lines.push_back({square, true});
        const TerrainEdit entry{1, terrainDrapeKind, std::make_shared<TerrainDrapeData>(data)};
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto plan = planTerrainDrape({*mesh, *stateOf(doc, terrain), recordOf(doc, terrain).parameters}, entry, true);
        const auto r = refusal([&] { (void)terrainDrapeOverlay(*mesh, plan, {false}, {{square}}, 0.01, 10); });
        check(r.code == "LIMIT_EXCEEDED", "an overlay over its face limit is refused");
    }
    std::cout << "overlay: group, lift, planar pieces, record unchanged, limit counted\n";
}

// Lines on a 1 m grid: one along grid lines (collinear with mesh edges), a diagonal through grid
// vertices (snapped), and lines crossing each other and an added breakline.
void gridLines() {
    Document doc;
    const auto terrain = importGrid(doc, 40);
    const double x0 = 5, y0 = 5;
    run(doc, command("terrain.add_source", terrain,
                     {{"role", "breakline"},
                      {"points", modelPoints(doc, terrain, {{x0 + 20.5, y0 + 1.5, 51}, {x0 + 20.5, y0 + 28.5, 51.2}})}}));
    const auto probes = probesIn(doc, terrain, {x0 - 1, y0 - 1, x0 + 31, y0 + 31}, 10000);
    const auto along = addPolyline(doc, modelLine(doc, terrain, {{x0, y0 + 10}, {x0 + 30, y0 + 10}}));
    const auto diagonal = addPolyline(doc, modelLine(doc, terrain, {{x0, y0}, {x0 + 30, y0 + 30}}));
    const auto cross = addPolyline(doc, modelLine(doc, terrain, {{x0 + 3.3, y0 + 25.7}, {x0 + 27.1, y0 + 2.9}}));
    QJsonArray edges = edgesOf(doc, along);
    for (const auto &value : edgesOf(doc, diagonal))
        edges.append(value);
    for (const auto &value : edgesOf(doc, cross))
        edges.append(value);
    const auto before = model(doc);
    const auto result = run(doc, drapeCommand(terrain, {{"edges", edges}}));
    const auto report = result["terrainReport"].toObject()["terrain"].toObject();
    std::cout << "grid: " << report["intersections"].toDouble() << " intersections, "
              << report["snapped"].toDouble() << " vertices snapped, " << report["sourcesSplit"].toDouble()
              << " source segments split\n";
    check(report["intersections"].toDouble() >= 2, "the lines cross each other");
    check(report["snapped"].toDouble() >= 20, "the lines pass through grid vertices");
    check(report["sourcesSplit"].toDouble() >= 1, "the lines split the breakline");
    {
        const auto state = stateOf(doc, terrain);
        std::size_t points = 0;
        for (const auto *source : state->sources())
            if (source->role == TerrainSourceRole::Breakline && !state->isDrapeSource(source->id))
                points = source->points.size();
        check(points == 5, "the breakline gains the three crossing points (" + std::to_string(points) + ")");
    }
    checkC4(doc, terrain, probes, "grid");
    checkRuns(doc, terrain, "grid");
    checkC1(doc, terrain, "grid");
    checkC6(doc, terrain, "grid");
    checkC7(doc, terrain, "grid");
    checkHistoryAndSave(doc, terrain, before, "grid");
    // Draping the same lines again changes no triangle (they are edges already).
    const auto again = run(doc, drapeCommand(terrain, {{"edges", edges}}));
    check(again["terrainReport"].toObject()["terrain"].toObject()["trianglesSplit"].toDouble() == 0,
          "draping the same lines again splits nothing");
    checkC1(doc, terrain, "grid again");
    checkC7(doc, terrain, "grid again");
    std::cout << "grid: along edges, through vertices, crossing lines and a breakline\n";
}

// Pads (decision 6): a drape over a pad's footprint or outline is refused and changes nothing; one
// across its slopes applies, supersedes the crossed rays, and C3 still holds.
void pads() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto c = centre(doc, terrain);
    const double h = groundAt(doc, terrain, c.x + 40, c.y) + 0.4;
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x + 40, c.y, 5))},
                                    {"elevation", toModel(doc, terrain, {0, 0, h}).z}}));
    const auto across = addPolyline(doc, modelLine(doc, terrain, {{c.x + 30, c.y + 1.3}, {c.x + 50, c.y + 1.3}}));
    checkUnchanged(doc, "a drape across a pad",
                   [&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, across)}})); },
                   "INVALID_REQUEST");
    const auto r = refusalReportKinds([&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, across)}})); });
    check(r.find("inside-pad") != std::string::npos, "the refusal names the pad: " + r);
    // Along the pad's outline edge exactly: refused too.
    const auto edge = addPolyline(doc, modelLine(doc, terrain, {{c.x + 35, c.y - 8}, {c.x + 35, c.y - 5}}));
    checkUnchanged(doc, "a drape touching a pad's outline",
                   [&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, edge)}})); },
                   "INVALID_REQUEST");
    // Across the slopes only (outside the footprint): applies; the crossed rays are superseded.
    const auto slopes = addPolyline(doc, modelLine(doc, terrain, {{c.x + 30, c.y + 6.3}, {c.x + 50, c.y + 6.3}}));
    const auto probes = probesIn(doc, terrain, {c.x + 28, c.y + 4, c.x + 52, c.y + 9}, 10000);
    run(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, slopes)}}));
    checkC4(doc, terrain, probes, "pads slopes");
    checkC3(doc, terrain, "pads slopes");
    checkC1(doc, terrain, "pads slopes");
    checkC6(doc, terrain, "pads slopes");
    checkC7(doc, terrain, "pads slopes");
    std::cout << "pads: footprint and outline refused, slopes crossed\n";
}

// A line passing 2e-7 m from a grid vertex (outside the 1e-7 m snap) would leave a sliver below
// the modelling tolerance: refused, located there, nothing changed. At 5e-8 m it snaps instead.
void sliver() {
    Document doc;
    const auto terrain = importGrid(doc, 30);
    const auto near = addPolyline(doc, modelLine(doc, terrain, {{4.5, 10 + 2e-7}, {15.5, 10 + 2e-7}}));
    checkUnchanged(doc, "a line 2e-7 m from a vertex",
                   [&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, near)}})); },
                   "INVALID_REQUEST");
    const auto r = refusal([&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, near)}})); });
    const auto refusals = r.report["terrain"].toObject()["refusals"].toArray();
    check(!refusals.isEmpty() && refusals.at(0).toObject()["kind"] == "degenerate-triangle" &&
              !refusals.at(0).toObject()["locations"].toArray().isEmpty(),
          "the sliver is refused with its location: " + refusalReportKinds([&] {
              direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, near)}}));
          }));
    const auto snapped = addPolyline(doc, modelLine(doc, terrain, {{4.5, 10 + 5e-8}, {15.5, 10 + 5e-8}}));
    const auto probes = probesIn(doc, terrain, {4, 9, 16, 11}, 10000);
    const auto result = run(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, snapped)}}));
    check(result["terrainReport"].toObject()["terrain"].toObject()["snapped"].toDouble() >= 10,
          "a line within 1e-7 m of the vertices snaps to them");
    checkC4(doc, terrain, probes, "snap");
    checkC1(doc, terrain, "snap");
    checkC6(doc, terrain, "snap");
    checkC7(doc, terrain, "snap");
    std::cout << "sliver: refused at 2e-7 m, snapped at 5e-8 m\n";
}

// Refusals: each wrong field, missing target and lock changes nothing.
void refusals() {
    Document doc;
    const auto terrain = importRings(doc, 12, 48);
    const auto c = centre(doc, terrain);
    const auto line = addPolyline(doc, modelLine(doc, terrain, {{c.x - 10, c.y}, {c.x + 10, c.y + 3}}));
    const auto edges = edgesOf(doc, line);
    auto refuses = [&](const std::string &what, QJsonObject fields, const char *code, bool keepMode = true) {
        auto cmd = keepMode ? drapeCommand(terrain, fields) : command("terrain.drape", terrain, fields);
        checkUnchanged(doc, what, [&] { direct(doc, cmd); }, code);
        // Through the batch (schema first, then the command): refused, nothing changed.
        const auto before = encoded(doc);
        const auto revision = doc.revision();
        bool refused = false;
        try {
            const auto response = executeBatch(doc, batch(doc, {cmd}));
            refused = response.contains("error");
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused && doc.revision() == revision && encoded(doc) == before,
              what + " (batch): refused and nothing changed");
    };
    refuses("no mode", {{"edges", edges}}, "INVALID_REQUEST", false);
    refuses("an unknown mode", {{"edges", edges}, {"mode", "project"}}, "INVALID_REQUEST");
    refuses("nothing to drape", {}, "INVALID_REQUEST");
    refuses("paint with overlay", {{"edges", edges}, {"mode", "overlay"}, {"paintRegions", true}}, "INVALID_REQUEST");
    refuses("lift with imprint", {{"edges", edges}, {"lift", 0.1}}, "INVALID_REQUEST");
    refuses("context with imprint", {{"edges", edges}, {"context", "0"}}, "INVALID_REQUEST");
    refuses("a negative lift", {{"edges", edges}, {"mode", "overlay"}, {"lift", -1}}, "INVALID_REQUEST");
    refuses("an unknown field", {{"edges", edges}, {"offset", 1}}, "INVALID_REQUEST");
    refuses("a missing edge", {{"edges", QJsonArray{QJsonObject{{"body", QString::number(line)}, {"edge", "999"}}}}},
            "NOT_FOUND");
    refuses("a missing face", {{"faces", QJsonArray{QJsonObject{{"body", QString::number(line)}, {"face", "999"}}}}},
            "NOT_FOUND");
    {
        const auto tile = terrainTiles(doc, terrain).front();
        const auto tileFace = doc.bodies().at(tile)->surface.faces.begin()->first;
        refuses("a terrain tile's face", {{"faces", QJsonArray{QJsonObject{{"body", QString::number(tile)},
                                                                           {"face", QString::number(tileFace)}}}}},
                "UNSUPPORTED_TARGET");
    }
    refuses("a context that is not a group",
            {{"edges", edges}, {"mode", "overlay"}, {"context", QString::number(line)}}, "UNSUPPORTED_TARGET");
    checkUnchanged(doc, "a missing terrain",
                   [&] { direct(doc, drapeCommand(999999, {{"edges", edges}})); }, "NOT_FOUND");
    // A vertical face has no plan area.
    const auto wall = doc.addFace({{toModel(doc, terrain, {c.x, c.y, 0}), toModel(doc, terrain, {c.x + 3, c.y, 0}),
                                    toModel(doc, terrain, {c.x + 3, c.y, 3}), toModel(doc, terrain, {c.x, c.y, 3})}},
                                  "Wall");
    refuses("a vertical face", {{"faces", facesOf(doc, wall)}}, "INVALID_REQUEST");
    // Locked terrain (imprint; overlay reads it only).
    {
        auto locked = std::make_shared<Body>(*doc.bodies().at(terrain));
        locked->locked = true;
        doc.apply({"Lock", {{terrain, doc.bodies().at(terrain), locked}}}, doc.revision());
        refuses("a locked terrain", {{"edges", edges}}, "INVALID_OPERATION");
        const auto overlay = run(doc, drapeCommand(terrain, {{"edges", edges}, {"mode", "overlay"}}));
        check(overlay["mode"] == "overlay", "overlay reads a locked terrain");
        doc.undo();
        doc.undo();
    }
    // A face of a locked body is refused, as a locked body's edges are (terrain.create_from_edges).
    {
        const auto slab = doc.addFace({modelLine(doc, terrain, {{c.x - 3, c.y - 3}, {c.x + 3, c.y - 3}, {c.x + 3, c.y + 3}, {c.x - 3, c.y + 3}})}, "Slab");
        auto locked = std::make_shared<Body>(*doc.bodies().at(slab));
        locked->locked = true;
        doc.apply({"Lock", {{slab, doc.bodies().at(slab), locked}}}, doc.revision());
        refuses("a locked body's face", {{"faces", facesOf(doc, slab)}}, "INVALID_OPERATION");
        auto lockedLine = std::make_shared<Body>(*doc.bodies().at(line));
        lockedLine->locked = true;
        doc.apply({"Lock", {{line, doc.bodies().at(line), lockedLine}}}, doc.revision());
        refuses("a locked body's edges", {{"edges", edges}}, "INVALID_OPERATION");
        doc.undo();
        doc.undo();
        doc.undo();
    }
    // Too many points.
    {
        std::vector<std::array<double, 2>> zig;
        for (int i = 0; i < 20005; ++i)
            zig.push_back({c.x - 30 + 0.003 * i, c.y + (i % 2 ? 0.5 : -0.5)});
        auto data = std::make_shared<TerrainDrapeData>();
        data->lines.push_back({zig, false});
        bool threw = false;
        try {
            validateTerrainDrapeData(*data);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        check(threw, "a drape of 20,005 points is refused");
    }
    std::cout << "refusals: each changes nothing\n";
}

// The codec round-trips resolved and unresolved entries and refuses malformed ones.
void codec() {
    Document doc;
    const auto terrain = importRings(doc, 12, 48);
    const auto c = centre(doc, terrain);
    const auto face = doc.addFace({modelLine(doc, terrain, {{c.x - 5, c.y - 5}, {c.x + 5, c.y - 5}, {c.x + 5, c.y + 5}, {c.x - 5, c.y + 5}})}, "Pad");
    const auto material = createMaterial(doc, "Asphalt", {0.2f, 0.2f, 0.2f});
    assignMaterial(doc, face, std::nullopt, material, true, true);
    const auto line = addPolyline(doc, modelLine(doc, terrain, {{c.x - 20, c.y + 1.7}, {c.x + 20, c.y + 1.9}}));
    QJsonArray edges = edgesOf(doc, line);
    run(doc, drapeCommand(terrain, {{"edges", edges}, {"faces", facesOf(doc, face)}, {"paintRegions", true}}));
    const auto entry = recordOf(doc, terrain).edits.back();
    const auto json = encodeTerrainEdit(*entry);
    check(json["lines"].isArray() && json["paint"].isArray() && json["runs"].isArray() && json["splits"].isArray(),
          "a resolved drape encodes its fields");
    check(*decodeTerrainEdit(json) == *entry, "the codec round-trips a resolved drape");
    auto unresolved = json;
    unresolved.remove("runs");
    unresolved.remove("splits");
    check(!dynamic_cast<const TerrainDrapeData &>(*decodeTerrainEdit(unresolved)->data).resolved(),
          "an unresolved drape round-trips");
    auto refuses = [](QJsonObject value, const std::string &what) {
        bool threw = false;
        try {
            (void)decodeTerrainEdit(value);
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw, "the decoder refuses " + what);
    };
    auto bad = json;
    bad["lift"] = 1;
    refuses(bad, "an unknown field");
    bad = json;
    bad["lines"] = QJsonArray{};
    refuses(bad, "no lines");
    bad = json;
    bad["lines"] = QJsonArray{QJsonObject{{"points", QJsonArray{QJsonArray{0, 0}}}}};
    refuses(bad, "a one-point line");
    bad = json;
    bad["lines"] = QJsonArray{QJsonObject{{"points", QJsonArray{QJsonArray{0, 0}, QJsonArray{1, 0}}}, {"closed", false}}};
    refuses(bad, "closed written as false");
    bad = json;
    bad.remove("runs");
    refuses(bad, "splits without runs");
    bad = json;
    {
        auto paint = bad["paint"].toArray();
        auto region = paint[0].toObject();
        region["material"] = 3;
        paint[0] = region;
        bad["paint"] = paint;
    }
    refuses(bad, "a numeric material");
    bad = json;
    {
        auto splits = bad["splits"].toArray();
        auto split = splits[0].toObject();
        const QJsonValue a = split["a"], b = split["b"];
        split["a"] = b;
        split["b"] = a;
        splits[0] = split;
        bad["splits"] = splits;
    }
    refuses(bad, "a split with its larger end first");
    bad = json;
    bad["runs"] = QJsonArray{QJsonArray{QJsonArray{0, 0, 0}}};
    refuses(bad, "a one-point run");
    // A crafted paint loop crossing one band too often is refused.
    bad = json;
    {
        QJsonArray comb;
        for (int i = 0; i < 600; ++i) {
            comb.append(QJsonArray{double(i), 0});
            comb.append(QJsonArray{double(i) + 0.5, 100});
        }
        bad["paint"] = QJsonArray{QJsonObject{{"loops", QJsonArray{comb}}, {"material", QString::number(material)}}};
    }
    refuses(bad, "a comb paint loop");
    std::cout << "codec: round trips and refusals\n";
}

// The in-app assistant has no drape tool and reads terrain.describe without `paintRegions`.
void assistantSurface() {
    Document doc;
    const auto terrain = importRings(doc, 12, 48);
    const auto c = centre(doc, terrain);
    const auto face = doc.addFace({modelLine(doc, terrain, {{c.x - 5, c.y - 5}, {c.x + 5, c.y - 5}, {c.x + 5, c.y + 5}, {c.x - 5, c.y + 5}})}, "Pad");
    const auto material = createMaterial(doc, "Asphalt", {0.2f, 0.2f, 0.2f});
    assignMaterial(doc, face, std::nullopt, material, true, true);
    run(doc, drapeCommand(terrain, {{"faces", facesOf(doc, face)}, {"paintRegions", true}}));
    const auto documentId = QString::fromStdString(doc.identity());
    const QJsonObject query{{"apiVersion", 1},
                            {"documentId", documentId},
                            {"expectedRevision", QString::number(doc.revision())},
                            {"query", "terrain.describe"},
                            {"body", QString::number(terrain)}};
    check(inspectDocument(doc, query)["data"].toObject().contains("paintRegions"),
          "the shared inspection (MCP, sessions) reports paint regions");
    AssistantTask::Options options;
    options.prompt = "Describe the terrain.";
    options.provider = "fixture";
    options.model = "fixture";
    options.context = QJsonArray{query};
    AssistantTask task(AssistantBackend{[&](const QJsonObject &request) { return inspectDocument(doc, request); },
                                        [&] {
                                            return QJsonObject{{"documentId", documentId},
                                                               {"revision", QString::number(doc.revision())}};
                                        }},
                       options);
    const auto pending = task.nextRequest();
    check(bool(pending), "the assistant asks the provider");
    const auto request = QJsonDocument(*pending).toJson(QJsonDocument::Compact);
    check(!request.contains("terrain.drape") && !request.contains("\"paintRegions\"") &&
              request.contains("\"drape\""),
          "the assistant has no drape tool, no paint regions, and reads the drape in its edits");
    std::cout << "assistant: terrain.drape hidden, paintRegions left out\n";
}

// Cancellation at each checkpoint of a drape's plan stops it there.
void cancelEverywhere() {
    Document doc;
    const auto terrain = importRings(doc, 12, 48);
    const auto c = centre(doc, terrain);
    auto data = std::make_shared<TerrainDrapeData>();
    for (int k = 0; k < 6; ++k)
        data->lines.push_back({{{c.x - 30, c.y - 15 + 5.3 * k}, {c.x + 30, c.y - 12 + 5.1 * k}}, false});
    data->lines.push_back({{{c.x - 3, c.y - 20}, {c.x + 2, c.y + 20}}, false});
    const TerrainEdit entry{1, terrainDrapeKind, data};
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    const auto state = stateOf(doc, terrain);
    int calls = 0;
    TerrainProgress counting([&] {
        ++calls;
        return false;
    });
    (void)planTerrainDrape({*mesh, *state, recordOf(doc, terrain).parameters, &counting}, entry);
    check(calls > 4, "the plan has checkpoints");
    for (int at = 1; at <= calls; ++at) {
        int seen = 0;
        TerrainProgress progress([&] { return ++seen >= at; });
        bool cancelled = false;
        try {
            (void)planTerrainDrape({*mesh, *state, recordOf(doc, terrain).parameters, &progress}, entry);
        } catch (const TerrainCancelled &) {
            cancelled = true;
        }
        check(cancelled && seen == at, "a cancellation at each checkpoint stops the plan there");
    }
    std::cout << "cancel: each of " << calls << " checkpoints stops the plan\n";
}

// Hostile inputs are refused quickly: lines that cross each other everywhere, a long serpentine
// across the whole terrain many times, and a paint region list past the terrain's band bound.
void hostile() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    const auto state = stateOf(doc, terrain);
    auto timed = [&](const std::string &what, const std::shared_ptr<TerrainDrapeData> &data) {
        const TerrainEdit entry{1, terrainDrapeKind, data};
        const auto start = std::chrono::steady_clock::now();
        const auto r = refusal([&] {
            (void)planTerrainDrape({*mesh, *state, recordOf(doc, terrain).parameters}, entry);
        });
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "hostile: " << what << ": " << (r.code.empty() ? "applied" : r.code) << " in " << seconds
                  << " s (" << r.message.substr(0, 140) << ")\n";
        return std::pair{r, seconds};
    };
    // A grid of 300 by 300 lines: 90,000 crossings among themselves.
    {
        auto data = std::make_shared<TerrainDrapeData>();
        for (int i = 0; i < 300; ++i) {
            data->lines.push_back({{{c.x - 150, c.y - 100 + 0.67 * i}, {c.x + 150, c.y - 100 + 0.67 * i + 0.3}}, false});
            data->lines.push_back({{{c.x - 100 + 0.68 * i, c.y - 140}, {c.x - 100 + 0.68 * i + 0.2, c.y + 140}}, false});
        }
        const auto [r, seconds] = timed("a 300 x 300 line grid", data);
        check(r.code == "LIMIT_EXCEEDED" && seconds < 30, "a dense line grid is refused by a bound");
    }
    // 10,000 segments, each across the whole terrain (a zigzag).
    {
        auto data = std::make_shared<TerrainDrapeData>();
        std::vector<std::array<double, 2>> zig;
        for (int i = 0; i < 10000; ++i)
            zig.push_back({c.x + (i % 2 ? 190.0 : -190.0), c.y - 140 + 0.028 * i});
        data->lines.push_back({zig, false});
        const auto [r, seconds] = timed("a 10,000-segment zigzag across the terrain", data);
        check(r.code == "LIMIT_EXCEEDED" && seconds < 30, "a zigzag across the terrain is refused by a bound");
    }
    // 20,000 points all within 1 mm: they cluster into one point.
    {
        auto data = std::make_shared<TerrainDrapeData>();
        std::vector<std::array<double, 2>> cloud;
        for (int i = 0; i < 20000; ++i)
            cloud.push_back({c.x + 1e-8 * (i % 100), c.y + 1e-8 * double(i / 100)});
        data->lines.push_back({cloud, false});
        const auto [r, seconds] = timed("20,000 points within 2 micrometres", data);
        check(!r.code.empty() && seconds < 30, "a point cloud as a line is refused");
    }
    // Lines along grid rows pass through every grid vertex: each segment is bent at 120 vertices,
    // and each piece tests only the edges beside it.
    {
        Document grid;
        const auto gridTerrain = importGrid(grid, 120);
        const auto gridMesh = terrainEditMeshCache().get(grid, gridTerrain, true);
        const auto gridState = stateOf(grid, gridTerrain);
        auto data = std::make_shared<TerrainDrapeData>();
        // A serpentine along the rows (the grid's whole width at whole Y), ten times over.
        const auto gc = centre(grid, gridTerrain);
        const double x0 = gc.x - 59.5, x1 = gc.x + 59.5, y0 = gc.y - 59.5;
        std::vector<std::array<double, 2>> rows;
        for (int pass = 0; pass < 10; ++pass)
            for (int y = 1; y <= 117; ++y) {
                const bool right = (pass * 117 + y) % 2;
                rows.push_back({right ? x0 : x1, y0 + y});
                rows.push_back({right ? x1 : x0, y0 + y});
            }
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        data->lines.push_back({rows, false});
        const TerrainEdit entry{1, terrainDrapeKind, data};
        const auto start = std::chrono::steady_clock::now();
        std::size_t resolvedPoints = 0, segments = rows.size();
        const auto r = refusal([&] {
            resolvedPoints = planTerrainDrape({*gridMesh, *gridState, recordOf(grid, gridTerrain).parameters}, entry).points;
        });
        std::cout << "hostile: rows: " << segments << " points in, " << resolvedPoints << " resolved\n";
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "hostile: rows through 120 grid vertices each: " << (r.code.empty() ? "applied" : r.code)
                  << " in " << seconds << " s (" << r.message.substr(0, 140) << ")\n";
        check(seconds < 30, "lines through many grid vertices are split or refused quickly");
    }
    std::cout << "hostile: refused within their bounds\n";
}

// Crafted files (review fixes): a drape whose split segment a source passes 10,000 times would
// grow the current sources by 655 million points; its effect is left out of the derivation (and
// a command that would do it is refused), quickly. A stored source edit naming a drape's line is
// left out of the derivation too, as the replay skips it.
void hostileFile() {
    Document doc;
    const auto terrain = importGrid(doc, 20);
    auto record = recordOf(doc, terrain);
    const auto state0 = stateOf(doc, terrain);
    const Vec3 a{3.25, 4.5, 50.1}, b{8.75, 4.5, 50.2};
    auto add = std::make_shared<TerrainSourceEditData>();
    add->source = terrainAddedSourceId(state0->maxBaseId(), 1);
    add->role = TerrainSourceRole::Breakline;
    for (int i = 0; i < 10000; ++i) {
        add->points.push_back(a);
        add->points.push_back(b);
    }
    auto drape = std::make_shared<TerrainDrapeData>();
    drape->lines.push_back({{{3.25, 2}, {3.25, 7}}, false});
    drape->runs.push_back({{3.25, 2, 50.1}, {3.25, 7, 50.2}});
    TerrainDrapeSplit split;
    split.a = {a.x, a.y};
    split.b = {b.x, b.y};
    for (int i = 0; i < 65536; ++i)
        split.points.push_back({a.x + 5.5 * (i + 1) / 65537.0, a.y, 50.15});
    drape->splits.push_back(split);
    validateTerrainDrapeData(*drape);
    record.edits.push_back(std::make_shared<TerrainEdit>(TerrainEdit{1, terrainAddSourceKind, add}));
    record.edits.push_back(std::make_shared<TerrainEdit>(TerrainEdit{2, terrainDrapeKind, drape}));
    // A source edit naming the drape's line (ordinal 0 of entry 2).
    auto edit = std::make_shared<TerrainSourceEditData>();
    edit->source = *terrainSplitSourceId(2, 0);
    edit->role = TerrainSourceRole::Breakline;
    edit->points = {{3.25, 2, 51}, {3.25, 7, 51}};
    edit->before = {3.25, 2, 3.25, 7};
    record.edits.push_back(std::make_shared<TerrainEdit>(TerrainEdit{3, terrainEditSourceKind, edit}));
    record.nextEditId = 4;
    const auto start = std::chrono::steady_clock::now();
    const auto state = terrainSourceState(record);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "hostile file: derived in " << seconds << " s\n";
    check(seconds < 10, "the crafted drape is derived quickly");
    check(state->find(add->source)->points.size() == 20000, "the crafted drape's effect is left out");
    check(!state->find(*terrainSplitSourceId(2, 0)), "so its lines are not added either");
    // A drape whose own effect stands: the source edit naming its line is left out.
    record.edits[1] = std::make_shared<TerrainEdit>(TerrainEdit{2, terrainDrapeKind, [&] {
        auto plain = std::make_shared<TerrainDrapeData>(*drape);
        plain->splits.clear();
        return plain;
    }()});
    const auto second = terrainSourceState(record);
    const auto *line = second->find(*terrainSplitSourceId(2, 0));
    check(line && line->points.front().z == 50.1 && second->isDrapeSource(line->id),
          "a stored source edit naming a drape's line is left out of the derivation");
    std::cout << "hostile file: crafted drape and source edit left out\n";
}

// Review fixes (PR #390). Re-elevating a contour a drape crossed is refused naming the drape's
// change, located; removing it is allowed and leaves the crossing points where the drape put them.
void crossedContour() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto c = centre(doc, terrain);
    const auto line = addPolyline(doc, modelLine(doc, terrain, {{c.x - 70, c.y + 3.3}, {c.x + 70, c.y + 5.1}}));
    run(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, line)}}));
    const auto drapeId = recordOf(doc, terrain).edits.back()->id;
    const auto state = stateOf(doc, terrain);
    std::set<TerrainPlanKey> crossing;
    for (const auto &run : lastDrape(doc, terrain).runs)
        for (const auto &p : run)
            crossing.insert(TerrainPlanKey::of(p.x, p.y));
    const TerrainSource *crossed = nullptr;
    for (const auto *source : state->sources())
        if (source->role == TerrainSourceRole::Contour)
            for (const auto &p : source->points)
                if (!crossed && crossing.contains(TerrainPlanKey::of(p.x, p.y)))
                    crossed = source;
    check(crossed, "the drape crossed a contour");
    const auto id = crossed->id;
    const double z = toModel(doc, terrain, {0, 0, crossed->points.front().z}).z;
    const auto edit = [&] {
        direct(doc, command("terrain.edit_source", terrain, {{"source", double(id)}, {"elevation", z + 0.3}}));
    };
    checkUnchanged(doc, "re-elevating a contour a drape crossed", edit, "INVALID_REQUEST");
    const auto r = refusal(edit);
    const auto refusals = r.report["terrain"].toObject()["refusals"].toArray();
    const auto problem = refusals.at(0).toObject();
    const auto message = problem["message"].toString().toStdString();
    check(problem["kind"] == "crossed-by-change" && !problem["locations"].toArray().isEmpty() &&
              message.find("Drape (change #" + std::to_string(drapeId) + ")") != std::string::npos &&
              message.find("terrain.remove_edit") != std::string::npos,
          "the refusal names the drape's change and is located: " + message);
    // Removing the contour keeps the crossing points (vertices of the drape's lines).
    run(doc, command("terrain.remove_source", terrain, {{"source", double(id)}}));
    checkC1(doc, terrain, "crossed contour removed");
    checkC6(doc, terrain, "crossed contour removed");
    checkC7(doc, terrain, "crossed contour removed");
    std::cout << "crossed contour: re-elevation refused naming change " << drapeId
              << ", removal keeps the crossing points\n";
}

// Review fixes (PR #390). A tile whose paint alone changes is charged one copy of its mesh (the
// history holds one copy at a time), about half a rebuilt tile's charge.
void paintHistory() {
    Document doc;
    const auto terrain = importPoints(doc);
    terrain_test_kinds::registerKinds();
    const auto tileId = terrainTiles(doc, terrain).front();
    const auto key = doc.bodies().at(tileId)->tileKey;
    const auto material = createMaterial(doc, "Gravel", {0.6f, 0.55f, 0.5f});
    auto charge = [&] { return doc.historyBytes() + doc.terrainHistoryBytes(); };
    const auto before = charge();
    auto painted = std::make_shared<Body>(*doc.bodies().at(tileId));
    for (const auto &[face, record] : painted->surface.faces)
        painted->faceMaterials[face] = {material, material};
    doc.apply({"Paint tile", {{tileId, doc.bodies().at(tileId), painted}}}, doc.revision());
    const double paint = double(charge() - before);
    // The same tile rebuilt, on a second document (an undone edit's bytes stay charged to redo).
    Document second;
    const auto other = importPoints(second);
    const auto otherBefore = second.historyBytes() + second.terrainHistoryBytes();
    applyTerrainEdit(second, other, "test.lift", terrain_test_kinds::lift(key, 0.5), "Lift");
    const double rebuild = double(second.historyBytes() + second.terrainHistoryBytes() - otherBefore);
    std::cout << "paint history: paint-only " << paint << " bytes, rebuild " << rebuild << " bytes\n";
    check(paint < 0.7 * rebuild && paint > 0.3 * rebuild,
          "a paint-only tile change is charged one copy of the tile");
}

// Review fixes (PR #390). Paint work is bounded per pass: a crafted record of 640 thin frames,
// each around the whole terrain, makes Regenerate refuse with a located limit rather than test
// every face against every frame; the pass checks for cancellation.
void paintWork() {
    Document doc;
    const auto terrain = importPoints(doc, 30000);
    const auto c = centre(doc, terrain);
    const auto material = createMaterial(doc, "Gravel", {0.6f, 0.55f, 0.5f});
    auto data = std::make_shared<TerrainDrapeData>();
    data->lines.push_back({{{c.x - 1, c.y}, {c.x + 1, c.y}}, false});
    data->runs.push_back({{c.x - 1, c.y, 120}, {c.x + 1, c.y, 120}});
    auto rect = [](double x0, double y0, double x1, double y1) {
        return std::vector<std::array<double, 2>>{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    };
    for (int k = 0; k < 640; ++k) { // 640 frames of two rectangles: 3,840 busiest-band edges
        const double e = 1 + 0.001 * k;
        data->paint.push_back({{rect(c.x - 210 + e, c.y - 160 + e, c.x + 210 - e, c.y + 160 - e),
                                rect(c.x - 210 + e + 0.0004, c.y - 160 + e + 0.0004,
                                     c.x + 210 - e - 0.0004, c.y + 160 - e - 0.0004)},
                               material});
    }
    validateTerrainDrapeData(*data);
    auto record = recordOf(doc, terrain);
    record.edits.push_back(std::make_shared<TerrainEdit>(TerrainEdit{record.nextEditId, terrainDrapeKind, data}));
    ++record.nextEditId;
    auto body = std::make_shared<Body>(*doc.bodies().at(terrain));
    body->terrain = record;
    doc.apply({"Crafted", {{terrain, doc.bodies().at(terrain), body}}}, doc.revision());
    // Regenerate: refused, located, in bounded time, changing nothing.
    const auto revision = doc.revision();
    const auto start = std::chrono::steady_clock::now();
    const auto request = parseTerrainReplayRequest(doc, command("terrain.regenerate", terrain));
    auto result = runTerrainReplay(request);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    bool located = false;
    for (const auto &value : result.report["terrain"].toObject()["refusals"].toArray())
        located = located || (value.toObject()["kind"] == "limit-exceeded" &&
                              !value.toObject()["locations"].toArray().isEmpty());
    std::cout << "paint work: Regenerate " << (result.ok ? "applied" : result.code) << " in " << seconds << " s\n";
    check(!result.ok && result.code == "LIMIT_EXCEEDED" && located, "the paint pass is refused, located");
    check(seconds < 60 && doc.revision() == revision, "quickly, and nothing changed");
    // The pass checks for cancellation.
    TerrainTileSet set;
    set.grid = record.tiling;
    for (const auto id : terrainTiles(doc, terrain))
        set.tiles.push_back(terrainTileContent(*doc.bodies().at(id)));
    TerrainProgress progress([] { return true; });
    bool cancelled = false;
    try {
        paintTerrainTileSet(set, record, &progress);
    } catch (const TerrainCancelled &) {
        cancelled = true;
    }
    check(cancelled, "a cancelled replay stops in the paint pass");
    std::cout << "paint work: bounded and cancellable\n";
}

// A cell over the triangle limit: an imprint that would pass the document's 250,000 terrain
// triangles is refused, located, naming the counts.
void triangleLimit() {
    Document doc;
    const auto terrain = importRings(doc);
    {
        const auto own = terrainTriangleCount(doc, terrain);
        const std::uint32_t columns = 483, rows = std::uint32_t((TerrainLimits::triangles - own - 200) / 2 / 482);
        TerrainMesh mesh;
        for (std::uint32_t j = 0; j <= rows; ++j)
            for (std::uint32_t i = 0; i < columns; ++i)
                mesh.vertices.push_back({5000.0 + i, double(j), 0.01 * i});
        for (std::uint32_t j = 0; j < rows; ++j)
            for (std::uint32_t i = 0; i + 1 < columns; ++i) {
                const auto v = j * columns + i;
                mesh.triangles.push_back({v, v + 1, v + columns + 1});
                mesh.triangles.push_back({v, v + columns + 1, v + columns});
            }
        TerrainRecord record;
        record.generator = "test/terrain-v1";
        createTerrain(doc, mesh, record);
    }
    const auto c = centre(doc, terrain);
    std::vector<std::array<double, 2>> zig;
    for (int i = 0; i < 400; ++i)
        zig.push_back({c.x - 60 + 0.3 * i, c.y + (i % 2 ? 9.0 : -9.0)});
    const auto line = addPolyline(doc, modelLine(doc, terrain, zig));
    const auto drape = [&] { direct(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, line)}})); };
    checkUnchanged(doc, "a drape past the triangle limit", drape, "LIMIT_EXCEEDED");
    const auto r = refusal(drape);
    const auto refusals = r.report["terrain"].toObject()["refusals"].toArray();
    check(!refusals.isEmpty() && refusals.at(0).toObject()["kind"] == "triangle-budget" &&
              !refusals.at(0).toObject()["locations"].toArray().isEmpty() &&
              r.message.find("250,000") != std::string::npos,
          "the limit refusal is located and names the counts");
    std::cout << "triangle limit: " << r.message.substr(0, 160) << "\n";
}

// A scripted sequence: a drape with paint, a sculpt across it, a pad beside it, a second drape
// crossing the first, a contour edit; the checks after each; Undo and Redo of all; reopen; Remove
// change of the first drape; Reset edits. The replay digest is pinned.
constexpr const char *sequenceDigest = "11e7453451a8b6fc7fcb1d55d66bc1b4896c82070e45aa5c87dbbcb622ddcb1a";
void sequence() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    cache.get(doc, terrain);
    const auto c = centre(doc, terrain);
    const auto start = model(doc);
    std::size_t steps = 0;
    const auto face = doc.addFace({modelLine(doc, terrain, {{c.x - 60, c.y - 2}, {c.x + 20, c.y - 2}, {c.x + 20, c.y + 2.5}, {c.x - 60, c.y + 2.5}})}, "Drive");
    const auto material = createMaterial(doc, "Gravel", {0.6f, 0.55f, 0.5f});
    assignMaterial(doc, face, std::nullopt, material, true, true);
    const auto line = addPolyline(doc, modelLine(doc, terrain, {{c.x - 10.3, c.y - 40}, {c.x - 8.1, c.y + 40}}));
    const auto startBodies = model(doc);
    (void)start;
    run(doc, drapeCommand(terrain, {{"faces", facesOf(doc, face)}, {"paintRegions", true}}));
    ++steps;
    checkAll(doc, terrain, cache, "sequence drape");
    run(doc, command("terrain.sculpt", terrain, {{"mode", "raise"},
                                                 {"path", planPoints(doc, terrain, {{c.x - 20, c.y - 10}, {c.x - 20, c.y + 10}})},
                                                 {"radius", 3},
                                                 {"amount", 0.3}}));
    ++steps;
    checkAll(doc, terrain, cache, "sequence sculpt");
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x + 45, c.y + 25, 4))},
                                    {"elevation", toModel(doc, terrain, {0, 0, groundAt(doc, terrain, c.x + 45, c.y + 25) + 0.2}).z}}));
    ++steps;
    checkAll(doc, terrain, cache, "sequence pad");
    checkC3(doc, terrain, "sequence pad");
    run(doc, drapeCommand(terrain, {{"edges", edgesOf(doc, line)}}));
    ++steps;
    checkAll(doc, terrain, cache, "sequence second drape");
    checkPaint(doc, terrain, material, {{c.x - 60, c.y - 2}, {c.x + 20, c.y - 2}, {c.x + 20, c.y + 2.5}, {c.x - 60, c.y + 2.5}}, {}, "sequence paint");
    const auto digest = recordOf(doc, terrain).meshDigest;
    std::cout << "sequence digest " << digest << "\n";
    if (std::string(sequenceDigest).size())
        check(digest == sequenceDigest, "the sequence's replay digest is pinned");
    const auto end = encoded(doc);
    for (std::size_t i = 0; i < steps; ++i)
        doc.undo();
    check(model(doc) == startBodies, "undoing the sequence restores the start");
    for (std::size_t i = 0; i < steps; ++i)
        doc.redo();
    check(encoded(doc) == end, "redoing the sequence restores the end");
    auto reopened = decodeDocument(encodeDocument(doc));
    checkC1(reopened, terrain, "sequence (reopened)");
    checkC3(reopened, terrain, "sequence (reopened)");
    checkC6(reopened, terrain, "sequence (reopened)");
    checkC7(reopened, terrain, "sequence (reopened)");
    const auto first = recordOf(doc, terrain).edits.front()->id;
    run(doc, command("terrain.remove_edit", terrain, {{"edit", double(first)}}));
    check(paintedCount(doc, terrain, material) == 0, "removing the painted drape removes its paint");
    checkC1(doc, terrain, "sequence remove");
    checkC3(doc, terrain, "sequence remove");
    checkC7(doc, terrain, "sequence remove");
    run(doc, command("terrain.reset_edits", terrain));
    check(recordOf(doc, terrain).edits.empty(), "Reset edits empties the list");
    std::cout << "sequence: " << steps << " changes undone, redone, reopened, removed and reset\n";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;
    const std::vector<std::pair<const char *, void (*)()>> tests{
        {"path", imprintPath},          {"clipping", clipping},
        {"paint", regionPaint},         {"overlay", overlay},
        {"grid", gridLines},            {"pads", pads},
        {"sliver", sliver},
        {"refusals", refusals},         {"codec", codec},
        {"assistant surface", assistantSurface}, {"cancel everywhere", cancelEverywhere},
        {"hostile", hostile},           {"hostile file", hostileFile},
        {"crossed contour", crossedContour}, {"paint history", paintHistory},
        {"paint work", paintWork},
        {"triangle limit", triangleLimit},
        {"sequence", sequence}};
    int failed = 0;
    for (const auto &[name, test] : tests) {
        if (argc > 1 && std::string(argv[1]) != name)
            continue;
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
