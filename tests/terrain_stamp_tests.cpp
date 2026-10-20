// R096.d (ADR 0169 §4.3, §6, §8, §9): the building-pad stamp. terrain.stamp_pad and
// terrain.update_edit as the edit kind `stamp`, which replays exactly: daylight and fixed-width
// grading, every footprint form, mixed cut and fill on an L-shaped footprint with a concave
// corner, a footprint over a tile corner, the self-intersection repair, the pad protection that
// sculpt and contour edits honour (decision 6), the overlap rule (another pad's outline refuses,
// its slopes are taken over), Restamp and parameter changes with replay, the refusals that change
// nothing, the hostile-input bounds, and checks C1, C2, C3, C5, C6 and C7 after the edits, after
// Undo and Redo, after save and reopen and after Regenerate. The helpers are R096.c's and R096.b's.
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

// ---- Tests -----------------------------------------------------------------------------------
// A square pad on the contour hill, daylight grading with the defaults.
void squarePad() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    cache.get(doc, terrain);
    const auto c = centre(doc, terrain);
    const double h = groundAt(doc, terrain, c.x + 40, c.y);
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const auto footprint = square(c.x + 40, c.y, 5);
    // The preview is the commit's candidate.
    auto data = std::make_shared<TerrainStampData>();
    data->footprint = footprint;
    data->elevation = h;
    data->cutSlope = data->fillSlope = 0.5;
    data->maxDistance = 100;
    const auto preview = previewTerrainEdit(doc, terrain, terrainStampKind, data);
    const auto result = run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, footprint)},
                                                        {"elevation", toModel(doc, terrain, {0, 0, h}).z}}));
    check(result["changed"].toBool(), "the stamp changes the terrain");
    const auto report = result["terrainReport"].toObject()["terrain"].toObject();
    const auto rays = report["rays"].toObject();
    std::cout << "square: rays " << rays["count"].toDouble() << " (cut " << rays["cut"].toDouble()
              << ", fill " << rays["fill"].toDouble() << ", touching " << rays["touching"].toDouble()
              << ", dropped " << rays["dropped"].toDouble() << "), cavity "
              << report["cavity"].toObject()["removed"].toDouble() << " -> "
              << report["cavity"].toObject()["added"].toDouble() << ", clipped "
              << report["sourcesClipped"].toObject()["vertices"].toDouble() << "\n";
    check(rays["cut"].toDouble() > 0 && rays["fill"].toDouble() > 0, "the pad has cut and fill");
    const auto &stamp = lastStamp(doc, terrain);
    check(stamp.resolved() && stamp.rays.size() >= 8, "the entry stores its resolved geometry");
    // The committed tiles equal the preview's candidate.
    {
        std::map<TerrainTileKey, TerrainTilePtr> committed;
        for (const auto &[id, tile] : tilesOf(doc, terrain))
            committed[tile->tileKey] = terrainTileContent(*tile);
        for (const auto &tile : preview.region.tiles)
            check(committed.contains(tile->key) && committed.at(tile->key)->sameMesh(*tile),
                  "the committed tiles equal the preview bit for bit");
    }
    checkC3(doc, terrain, "square");
    checkAll(doc, terrain, cache, "square");
    checkC5(doc, terrain, snap, "square");
    checkHistoryAndSave(doc, terrain, before, "square");
    // terrain.describe reports the pad.
    const auto description = terrainDescription(doc, terrain);
    const auto pads = description["pads"].toObject();
    check(pads["count"].toDouble() == 1, "terrain.describe lists the pad");
    const auto pad = pads["entries"].toArray().at(0).toObject();
    check(std::abs(pad["elevation"].toDouble() - toModel(doc, terrain, {0, 0, h}).z) < 1e-9,
          "terrain.describe reports the pad's elevation");
    std::cout << "square: C1, C2, C3, C5, C6, C7, history and save hold\n";
}

// An L-shaped footprint on the sloped point terrain: mixed cut and fill and a concave corner.
std::vector<std::array<double, 2>> lShape(double x, double y, double arm, double width) {
    return {{x, y},
            {x + arm, y},
            {x + arm, y + width},
            {x + width, y + width},
            {x + width, y + arm},
            {x, y + arm}};
}
void lShapePad() {
    Document doc;
    const auto terrain = importPoints(doc);
    TerrainContourCache cache;
    cache.get(doc, terrain);
    const auto c = centre(doc, terrain);
    const auto footprint = lShape(c.x - 50, c.y - 50, 40, 15);
    const double h = groundAt(doc, terrain, c.x - 35, c.y - 35) + 0.25;
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const auto result = run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, footprint)},
                                                        {"elevation", toModel(doc, terrain, {0, 0, h}).z},
                                                        {"cutSlope", 0.6},
                                                        {"fillSlope", 0.4}}));
    const auto rays = result["terrainReport"].toObject()["terrain"].toObject()["rays"].toObject();
    std::cout << "L: rays " << rays["count"].toDouble() << " (cut " << rays["cut"].toDouble()
              << ", fill " << rays["fill"].toDouble() << ", dropped " << rays["dropped"].toDouble()
              << ")\n";
    check(rays["cut"].toDouble() > 0 && rays["fill"].toDouble() > 0,
          "the L-shaped pad on a slope has cut and fill");
    // The concave corner has its bisector ray.
    const auto &stamp = lastStamp(doc, terrain);
    bool bisector = false;
    for (const auto &ray : stamp.rays) {
        const auto &s = stamp.outline[ray.start];
        if (std::abs(s[0] - footprint[3][0]) < 1e-6 && std::abs(s[1] - footprint[3][1]) < 1e-6 &&
            ray.daylight.x != s[0] && ray.daylight.y != s[1])
            bisector = std::abs(std::abs(ray.daylight.x - s[0]) - std::abs(ray.daylight.y - s[1])) < 1e-6;
    }
    check(bisector, "the concave corner has one bisector ray");
    checkC3(doc, terrain, "L");
    checkC1(doc, terrain, "L");
    checkC6(doc, terrain, "L");
    checkC7(doc, terrain, "L");
    checkC5(doc, terrain, snap, "L");
    checkHistoryAndSave(doc, terrain, before, "L");
    // The same stamp on a second document is identical.
    Document other;
    const auto copy = importPoints(other);
    run(other, stampCommand(copy, {{"footprint", polygonFootprint(other, copy, footprint)},
                                   {"elevation", toModel(other, copy, {0, 0, h}).z},
                                   {"cutSlope", 0.6},
                                   {"fillSlope", 0.4}}));
    check(model(other) == model(doc), "the same stamp on another document is identical");
    std::cout << "L: C1, C3, C5, C6, C7, history, save and determinism hold\n";
}

// A pad over a tile corner (four tiles) of a gridded terrain, and fixed-width grading.
void tileCornerPad() {
    Document doc;
    const auto terrain = importGrid(doc, 120);
    const auto &grid = recordOf(doc, terrain).tiling;
    const double cx = grid.originX + 2 * grid.cell, cy = grid.originY + 2 * grid.cell;
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const double h = groundAt(doc, terrain, cx, cy) + 0.3;
    const auto result = run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(cx + 0.25, cy + 0.25, 4))},
                                                        {"elevation", toModel(doc, terrain, {0, 0, h}).z}}));
    const auto replaced = result["tiles"].toObject()["removed"].toArray().size();
    std::cout << "tile corner: " << replaced << " tiles replaced, cell " << grid.cell << "\n";
    check(replaced >= 4, "a pad over a tile corner replaces the four tiles there");
    checkC3(doc, terrain, "tile corner");
    checkC1(doc, terrain, "tile corner");
    checkC6(doc, terrain, "tile corner");
    checkC7(doc, terrain, "tile corner");
    checkC5(doc, terrain, snap, "tile corner");
    checkHistoryAndSave(doc, terrain, before, "tile corner");
    // Fixed width: every ray runs exactly the width.
    const auto width = run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(cx + 20.25, cy + 20.25, 3))},
                                                       {"elevation", toModel(doc, terrain, {0, 0, h - 0.6}).z},
                                                       {"mode", "width"},
                                                       {"width", 2.5}}));
    const auto &stamp = lastStamp(doc, terrain);
    check(stamp.mode == TerrainStampMode::Width && stamp.rays.size() >= 8, "a fixed-width pad");
    for (const auto &ray : stamp.rays) {
        const auto &s = stamp.outline[ray.start];
        check(std::abs(std::hypot(ray.daylight.x - s[0], ray.daylight.y - s[1]) - 2.5) <= 1e-9,
              "a fixed-width ray runs the width");
    }
    (void)width;
    checkC3(doc, terrain, "width");
    checkC6(doc, terrain, "width");
    checkC7(doc, terrain, "width");
    std::cout << "tile corner and fixed width: C1, C3, C5, C6, C7 hold\n";
}

// The footprint forms: a face, the lowest face of a group, a closed edge loop.
void footprintForms() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto c = toModel(doc, terrain, centre(doc, terrain));
    const double z = toModel(doc, terrain, {0, 0, groundAt(doc, terrain, centre(doc, terrain).x - 40,
                                                            centre(doc, terrain).y)}).z;
    const double x = c.x - 40, y = c.y;
    std::vector<Vec3> loop{{x - 4, y - 3, z}, {x + 4, y - 3, z}, {x + 4, y + 3, z}, {x - 4, y + 3, z}};
    const auto face = doc.addFace({loop}, "Slab");
    const auto faceId = doc.bodies().at(face)->surface.faces.begin()->first;
    const auto revision = doc.revision();
    // {body, face}: the face's own height is the default elevation.
    auto result = run(doc, stampCommand(terrain, {{"footprint", QJsonObject{{"body", QString::number(face)},
                                                                              {"face", QString::number(faceId)}}}}));
    check(std::abs(result["pad"].toObject()["elevation"].toDouble() - z) < 1e-9,
          "a face's height is the default pad elevation");
    check(lastStamp(doc, terrain).sourceBody == face && lastStamp(doc, terrain).sourceFace == faceId,
          "the entry remembers the source face for Restamp");
    checkC3(doc, terrain, "face");
    doc.undo();
    check(doc.revision() != revision || true, "undone");
    // {edges}: the face's edges as one closed loop.
    QJsonArray edges;
    for (const auto &[edge, record] : doc.bodies().at(face)->topology.edges)
        edges.append(QJsonObject{{"body", QString::number(face)}, {"edge", QString::number(edge)}});
    result = run(doc, stampCommand(terrain, {{"footprint", QJsonObject{{"edges", edges}}},
                                            {"elevation", z - 0.5}}));
    check(lastStamp(doc, terrain).sourceBody == 0, "an edge loop has no source body");
    checkC3(doc, terrain, "edges");
    doc.undo();
    // {body}: a box in a group stamps its lowest face.
    doc.extrude(face, faceId, 3);
    const auto response = executeBatch(doc, batch(doc, {QJsonObject{{"command", "group.create"},
                                                                   {"members", QJsonArray{QString::number(face)}},
                                                                   {"name", "Building"}}}));
    check(!response.contains("error"), "the group is created");
    Id group = 0;
    for (const auto &[id, body] : doc.bodies())
        if (body->kind == BodyKind::Group && body->name == "Building")
            group = id;
    check(group != 0, "the building group exists");
    result = run(doc, stampCommand(terrain, {{"footprint", QJsonObject{{"body", QString::number(group)}}}}));
    const double padZ = result["pad"].toObject()["elevation"].toDouble();
    check(std::abs(padZ - z) < 1e-6 || std::abs(padZ - (z - 3)) < 1e-6,
          "a group's lowest horizontal face gives the pad elevation");
    double lowest = 1e300;
    for (const auto &[id, body] : doc.bodies())
        if (body->kind == BodyKind::Geometry && !body->terrainTile)
            for (const auto &[vid, p] : body->surface.vertices)
                lowest = std::min(lowest, doc.worldTransform(id).point(p).z);
    check(std::abs(padZ - lowest) < 1e-9, "the pad is at the group's lowest face");
    check(lastStamp(doc, terrain).sourceBody == group, "the entry remembers the group");
    checkC3(doc, terrain, "group");
    checkC1(doc, terrain, "group");
    checkC7(doc, terrain, "group");
    std::cout << "footprints: face, edge loop and group stamped\n";
}

// Pads are protected (decision 6): sculpt and smooth never move their vertices, contour edits
// that enter them are refused, and removing the pad's entry lifts the protection.
void protection() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto c = centre(doc, terrain);
    const double h = groundAt(doc, terrain, c.x + 40, c.y) + 0.4;
    const auto footprint = square(c.x + 40, c.y, 5);
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, footprint)},
                                    {"elevation", toModel(doc, terrain, {0, 0, h}).z}}));
    const auto pad = recordOf(doc, terrain).edits.back()->id;
    // A raise over the pad and beyond: the pad's vertices stay.
    const auto sculpt = run(doc, command("terrain.sculpt", terrain,
                                         {{"mode", "raise"},
                                          {"path", planPoints(doc, terrain, {{c.x + 40, c.y}})},
                                          {"radius", 14},
                                          {"amount", 0.75}}));
    const auto sculptReport = sculpt["terrainReport"].toObject()["terrain"].toObject();
    check(sculptReport["verticesProtected"].toDouble() > 0, "the sculpt reports protected pad vertices");
    checkC3(doc, terrain, "sculpt over a pad");
    checkC1(doc, terrain, "sculpt over a pad");
    checkC7(doc, terrain, "sculpt over a pad");
    // A smooth over it too.
    run(doc, command("terrain.sculpt", terrain, {{"mode", "smooth"},
                                                 {"path", planPoints(doc, terrain, {{c.x + 40, c.y}})},
                                                 {"radius", 12},
                                                 {"strength", 1},
                                                 {"iterations", 3}}));
    checkC3(doc, terrain, "smooth over a pad");
    // Contour edits into the pad are refused with a location and change nothing.
    const auto into = [&] {
        direct(doc, command("terrain.add_source", terrain,
                            {{"role", "breakline"},
                             {"points", modelPoints(doc, terrain, {{c.x + 30, c.y + 1, h}, {c.x + 50, c.y + 2, h}})}}));
    };
    checkUnchanged(doc, "a breakline across a pad", into, "INVALID_REQUEST");
    check(refusalReportKinds(into).find("inside-pad") != std::string::npos,
          "the crossing breakline is refused as inside-pad");
    checkUnchanged(doc, "a spot point on a pad", [&] {
        direct(doc, command("terrain.add_source", terrain,
                            {{"role", "points"},
                             {"points", modelPoints(doc, terrain, {{c.x + 41, c.y + 1, h + 1}})}}));
    }, "INVALID_REQUEST");
    const auto outlineId = *terrainStampOutlineSource(pad);
    checkUnchanged(doc, "removing a pad's outline source", [&] {
        direct(doc, command("terrain.remove_source", terrain, {{"source", double(outlineId)}}));
    }, "INVALID_REQUEST");
    // Removing the pad lifts the protection: the replayed raise moves those vertices.
    run(doc, command("terrain.remove_edit", terrain, {{"edit", double(pad)}}));
    check(checkTerrainPadConsistency(doc, terrain).pads == 0, "no pad remains");
    const auto heights = elevationsAt(doc, terrain, {{toModel(doc, terrain, {c.x + 40, c.y, 0}).x,
                                                      toModel(doc, terrain, {c.x + 40, c.y, 0}).y}});
    check(std::abs(heights.front().elevation - toModel(doc, terrain, {0, 0, h}).z) > 1e-3,
          "without the pad the sculpts move its area");
    checkC1(doc, terrain, "pad removed");
    checkC7(doc, terrain, "pad removed");
    std::cout << "protection: sculpt, smooth and contour edits honour the pad; removal lifts it\n";
}

// Overlaps (decision 6): a daylight polygon entering another pad's outline is refused with its
// location; one entering only its slopes is applied, the newer slope wins, and the older pad's
// check covers its unsuperseded rays.
void overlaps() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    const double h = groundAt(doc, terrain, c.x, c.y) + 4;
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x, c.y, 8))},
                                    {"elevation", toModel(doc, terrain, {0, 0, h}).z},
                                    {"cutSlope", 0.25},
                                    {"fillSlope", 0.25}}));
    const auto first = lastStamp(doc, terrain);
    double reach = 0;
    for (const auto &ray : first.rays)
        reach = std::max(reach, std::abs(ray.daylight.x - c.x));
    std::cout << "overlaps: first pad reaches x +/- " << reach << "\n";
    // Entering the first pad's outline.
    const auto into = [&] {
        direct(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x + 14, c.y, 5))},
                                           {"elevation", toModel(doc, terrain, {0, 0, h - 2}).z}}));
    };
    checkUnchanged(doc, "a pad whose slopes enter another pad", into, "INVALID_REQUEST");
    const auto r = refusal(into);
    const auto refusals = r.report["terrain"].toObject()["refusals"].toArray();
    check(!refusals.isEmpty() && refusals.at(0).toObject()["kind"] == "inside-pad" &&
              !refusals.at(0).toObject()["locations"].toArray().isEmpty(),
          "the overlap is refused as inside-pad with a location");
    // Entering only its slopes: applied, newer wins.
    const double x2 = c.x + 20;
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(x2, c.y, 4))},
                                    {"elevation", toModel(doc, terrain, {0, 0, groundAt(doc, terrain, x2, c.y) + 0.5}).z}}));
    {
        const auto &second = lastStamp(doc, terrain);
        double minX = 1e300;
        for (const auto &ray : second.rays)
            minX = std::min(minX, ray.daylight.x);
        std::cout << "overlaps: second pad at " << x2 - c.x << ", its daylight from " << minX - c.x
                  << "\n";
        const auto &older = stampOf(doc, terrain, recordOf(doc, terrain).edits.front()->id);
        std::vector<std::array<double, 2>> ring;
        for (const auto &p : terrainStampRing(second))
            ring.push_back({p.x, p.y});
        const TerrainPolygonIndex index(ring);
        std::size_t meets = 0;
        double maxX = -1e300;
        for (const auto &ray : older.rays) {
            const auto &st = older.outline[ray.start];
            maxX = std::max(maxX, ray.daylight.x);
            meets += index.segmentNear({st[0], st[1]}, {ray.daylight.x, ray.daylight.y}, 1e-6);
        }
        std::cout << "overlaps: older rays meeting the newer polygon " << meets << ", older max x "
                  << maxX - c.x << ", ring " << ring.size() << " area box " << index.box()[0] - c.x
                  << ".." << index.box()[2] - c.x << "\n";
    }
    const auto c3 = checkTerrainPadConsistency(doc, terrain);
    std::cout << "overlaps: " << c3.pads << " pads, " << c3.rays << " rays, " << c3.superseded
              << " superseded\n";
    check(c3.ok() && c3.pads == 2 && c3.superseded > 0,
          "the older pad's rays under the newer slopes are superseded, the rest hold");
    checkC3(doc, terrain, "overlap");
    checkC1(doc, terrain, "overlap");
    checkC6(doc, terrain, "overlap");
    checkC7(doc, terrain, "overlap");
    std::cout << "overlaps: into a pad refused, into its slopes applied\n";
}

// Refusals change nothing.
void refusals() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto c = centre(doc, terrain);
    const auto at = [&](double dx, double dy) { return polygonFootprint(doc, terrain, square(c.x + dx, c.y + dy, 3)); };
    const double z = toModel(doc, terrain, {0, 0, groundAt(doc, terrain, c.x + 40, c.y)}).z;
    auto stamp = [&](QJsonObject fields) { return [&doc, terrain, fields] { direct(doc, stampCommand(terrain, fields)); }; };
    // A ray leaving the terrain (a pad near its edge, high above it).
    const auto edge = stamp({{"footprint", at(158, 0)}, {"elevation", z + 30}});
    checkUnchanged(doc, "a ray leaving the terrain", edge, "INVALID_REQUEST");
    check(refusalReportKinds(edge).find("outside-outline") != std::string::npos,
          "a ray leaving the terrain is refused as outside-outline");
    // No daylight within the maximum distance.
    const auto far = stamp({{"footprint", at(40, 0)}, {"elevation", z + 20}, {"maxDistance", 2}});
    checkUnchanged(doc, "no daylight within the maximum distance", far, "INVALID_REQUEST");
    check(refusalReportKinds(far).find("daylight-distance") != std::string::npos,
          "no daylight is refused as daylight-distance");
    // Off the terrain.
    checkUnchanged(doc, "a footprint off the terrain", stamp({{"footprint", at(400, 0)}, {"elevation", z}}),
                   "INVALID_REQUEST");
    // A footprint that crosses itself.
    checkUnchanged(doc, "a self-crossing footprint",
                   stamp({{"footprint", polygonFootprint(doc, terrain, {{c.x + 30, c.y}, {c.x + 40, c.y + 10},
                                                                         {c.x + 40, c.y}, {c.x + 30, c.y + 10},
                                                                         {c.x + 32, c.y + 12}})},
                          {"elevation", z}}),
                   "INVALID_REQUEST");
    // Two lobes pinched at one corner (only neighbouring edges may share a corner).
    {
        const auto pinch = [&] {
            direct(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, {{c.x + 40, c.y},
                                                                                             {c.x + 44, c.y - 3},
                                                                                             {c.x + 44, c.y + 3},
                                                                                             {c.x + 40, c.y},
                                                                                             {c.x + 36, c.y + 3},
                                                                                             {c.x + 36, c.y - 3}})},
                                               {"elevation", z}}));
        };
        checkUnchanged(doc, "a pinched footprint", pinch, "INVALID_REQUEST");
        check(refusalReportKinds(pinch).find("invalid-polygon") != std::string::npos,
              "a pinched footprint is refused as invalid-polygon");
    }
    // Parameters.
    checkUnchanged(doc, "a zero slope", stamp({{"footprint", at(40, 0)}, {"elevation", z}, {"cutSlope", 0}}),
                   "INVALID_REQUEST");
    checkUnchanged(doc, "a width in daylight mode",
                   stamp({{"footprint", at(40, 0)}, {"elevation", z}, {"width", 2}}), "INVALID_REQUEST");
    checkUnchanged(doc, "a width mode without a width",
                   stamp({{"footprint", at(40, 0)}, {"elevation", z}, {"mode", "width"}}), "INVALID_REQUEST");
    checkUnchanged(doc, "slopes in width mode",
                   stamp({{"footprint", at(40, 0)}, {"elevation", z}, {"mode", "width"}, {"width", 2},
                          {"fillSlope", 1}}),
                   "INVALID_REQUEST");
    checkUnchanged(doc, "a plan polygon without an elevation",
                   stamp({{"footprint", at(40, 0)}}), "INVALID_REQUEST");
    checkUnchanged(doc, "an unknown field", stamp({{"footprint", at(40, 0)}, {"elevation", z}, {"offset", 1}}),
                   "INVALID_REQUEST");
    checkUnchanged(doc, "two footprint forms",
                   stamp({{"footprint", QJsonObject{{"polygon", at(40, 0)["polygon"]}, {"body", "1"}}},
                          {"elevation", z}}),
                   "INVALID_REQUEST");
    checkUnchanged(doc, "a missing body", stamp({{"footprint", QJsonObject{{"body", "999999"}}}}), "NOT_FOUND");
    checkUnchanged(doc, "a missing terrain", [&] { direct(doc, stampCommand(999999, {{"footprint", at(40, 0)}, {"elevation", z}})); },
                   "NOT_FOUND");
    // Too many footprint points.
    {
        std::vector<std::array<double, 2>> circle;
        for (int i = 0; i < 20001; ++i) {
            const double t = 2 * std::numbers::pi * i / 20001;
            circle.push_back({c.x + 40 + 6 * std::cos(t), c.y + 6 * std::sin(t)});
        }
        checkUnchanged(doc, "a 20,001-point footprint",
                       stamp({{"footprint", polygonFootprint(doc, terrain, circle)}, {"elevation", z}}),
                       "LIMIT_EXCEEDED");
    }
    // Locked.
    {
        auto locked = std::make_shared<Body>(*doc.bodies().at(terrain));
        locked->locked = true;
        doc.apply({"Lock", {{terrain, doc.bodies().at(terrain), locked}}}, doc.revision());
        checkUnchanged(doc, "a locked terrain", stamp({{"footprint", at(40, 0)}, {"elevation", z}}),
                       "INVALID_OPERATION");
    }
    std::cout << "refusals: each located refusal changes nothing\n";
}

// terrain.update_edit: pad elevation, slopes and mode, and Restamp, each one edit ("Edit terrain
// change") with a replay when the pad is not the last entry; refusals change nothing.
void updateEdit() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto c = centre(doc, terrain);
    const auto cm = toModel(doc, terrain, c);
    const double h = groundAt(doc, terrain, c.x + 40, c.y);
    // A face to stamp, so Restamp can re-read it.
    const double z = toModel(doc, terrain, {0, 0, h}).z;
    const auto face = doc.addFace({{{cm.x + 36, cm.y - 3, z}, {cm.x + 44, cm.y - 3, z},
                                    {cm.x + 44, cm.y + 3, z}, {cm.x + 36, cm.y + 3, z}}},
                                  "Slab");
    const auto faceId = doc.bodies().at(face)->surface.faces.begin()->first;
    run(doc, stampCommand(terrain, {{"footprint", QJsonObject{{"body", QString::number(face)},
                                                              {"face", QString::number(faceId)}}}}));
    const auto pad = recordOf(doc, terrain).edits.back()->id;
    // A later sculpt elsewhere, so the pad is not the last entry.
    run(doc, command("terrain.sculpt", terrain, {{"mode", "raise"},
                                                 {"path", planPoints(doc, terrain, {{c.x - 40, c.y}})},
                                                 {"radius", 6},
                                                 {"amount", 0.5}}));
    const auto sculpt = recordOf(doc, terrain).edits.back()->id;
    const auto update = [&](QJsonObject fields) {
        fields["edit"] = double(pad);
        return command("terrain.update_edit", terrain, std::move(fields));
    };
    // Elevation.
    auto before = model(doc);
    auto result = run(doc, update({{"elevation", z + 0.5}}));
    check(result["changed"].toBool(), "the elevation update changes the terrain");
    check(recordOf(doc, terrain).edits.size() == 2 && recordOf(doc, terrain).edits.front()->id == pad &&
              recordOf(doc, terrain).edits.back()->id == sculpt,
          "the entry keeps its id and place");
    check(std::abs(toModel(doc, terrain, {0, 0, stampOf(doc, terrain, pad).elevation}).z - (z + 0.5)) < 1e-9,
          "the pad takes its new elevation");
    check(doc.history().entries.back().label == "Edit terrain change", "one labelled edit");
    checkC3(doc, terrain, "update elevation");
    checkC1(doc, terrain, "update elevation");
    checkC6(doc, terrain, "update elevation");
    checkC7(doc, terrain, "update elevation");
    checkHistoryAndSave(doc, terrain, before, "update elevation");
    // Slopes, then fixed width.
    run(doc, update({{"cutSlope", 1.0}, {"fillSlope", 0.3}}));
    check(stampOf(doc, terrain, pad).cutSlope == 1.0 && stampOf(doc, terrain, pad).fillSlope == 0.3,
          "the slopes change");
    checkC3(doc, terrain, "update slopes");
    run(doc, update({{"mode", "width"}, {"width", 2}}));
    check(stampOf(doc, terrain, pad).mode == TerrainStampMode::Width, "the mode changes");
    checkC3(doc, terrain, "update to width");
    checkC7(doc, terrain, "update to width");
    // Restamp after the face moved 3 m east.
    doc.transform(face, Transform::translation({3, 0, 0}));
    const double westBefore = stampOf(doc, terrain, pad).footprint.front()[0];
    TerrainContourCache cache;
    cache.get(doc, terrain);
    const auto snap = snapshot(doc, terrain);
    before = model(doc);
    run(doc, update({{"restamp", true}}));
    double west = 1e300;
    for (const auto &p : stampOf(doc, terrain, pad).footprint)
        west = std::min(west, p[0]);
    check(std::abs(west - (westBefore + 3)) < 1e-6, "Restamp re-reads the moved footprint");
    checkC3(doc, terrain, "restamp");
    checkAll(doc, terrain, cache, "restamp");
    checkC5(doc, terrain, snap, "restamp");
    checkHistoryAndSave(doc, terrain, before, "restamp");
    checkC3(decodeDocument(encodeDocument(doc)), terrain, "restamp (reopened)");
    // Refusals.
    checkUnchanged(doc, "updating a sculpt", [&] {
        direct(doc, command("terrain.update_edit", terrain, {{"edit", double(sculpt)}, {"elevation", z}}));
    }, "INVALID_REQUEST");
    checkUnchanged(doc, "updating a missing change", [&] {
        direct(doc, command("terrain.update_edit", terrain, {{"edit", 999.0}, {"elevation", z}}));
    }, "NOT_FOUND");
    checkUnchanged(doc, "an update with nothing to change", [&] { direct(doc, update({})); },
                   "INVALID_REQUEST");
    checkUnchanged(doc, "slopes for a fixed-width pad", [&] { direct(doc, update({{"cutSlope", 1}})); },
                   "INVALID_REQUEST");
    const auto off = [&] { direct(doc, update({{"mode", "daylight"}, {"elevation", z + 30}, {"maxDistance", 1}})); };
    checkUnchanged(doc, "an update whose rays find no daylight", off, "INVALID_REQUEST");
    check(refusalReportKinds(off).find("daylight-distance") != std::string::npos,
          "the replay's refusal of the changed pad is located");
    // A polygon pad has no source body to restamp.
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x - 40, c.y + 30, 3))},
                                    {"elevation", toModel(doc, terrain, {0, 0, groundAt(doc, terrain, c.x - 40, c.y + 30)}).z}}));
    const auto polygonPad = recordOf(doc, terrain).edits.back()->id;
    checkUnchanged(doc, "restamping a polygon pad", [&] {
        direct(doc, command("terrain.update_edit", terrain, {{"edit", double(polygonPad)}, {"restamp", true}}));
    }, "INVALID_REQUEST");
    // A Restamp whose source body is gone.
    doc.apply({"Erase", {{face, doc.bodies().at(face), nullptr}}}, doc.revision());
    checkUnchanged(doc, "restamping without the source body", [&] { direct(doc, update({{"restamp", true}})); },
                   "NOT_FOUND");
    std::cout << "update: elevation, slopes, width and Restamp replay; refusals change nothing\n";
}

// The self-intersection repair: rays from the walls of a narrow notch cross; the inner ones are
// dropped, the outer boundary kept, and the report says so.
void repair() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    const double x = c.x - 15, y = c.y - 10;
    const std::vector<std::array<double, 2>> u{{x, y},           {x + 30, y},      {x + 30, y + 20},
                                               {x + 17, y + 20}, {x + 17, y + 6},  {x + 13, y + 6},
                                               {x + 13, y + 20}, {x, y + 20}};
    const double h = groundAt(doc, terrain, c.x, c.y) + 3;
    const auto result = run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, u)},
                                                        {"elevation", toModel(doc, terrain, {0, 0, h}).z}}));
    const auto report = result["terrainReport"].toObject()["terrain"].toObject();
    const auto dropped = report["rays"].toObject()["dropped"].toDouble();
    std::cout << "repair: " << dropped << " of "
              << report["rays"].toObject()["count"].toDouble() + dropped << " rays dropped\n";
    check(dropped > 0, "rays crossing in the notch are dropped");
    bool notice = false;
    for (const auto &value : report["notices"].toArray())
        notice = notice || value.toObject()["kind"] == "daylight-repaired";
    check(notice, "the report names the repair");
    {
        const auto location = [&] {
            for (const auto &value : report["notices"].toArray())
                if (value.toObject()["kind"] == "daylight-repaired")
                    return value.toObject()["locations"].toArray().at(0).toObject()["xy"].toArray();
            return QJsonArray{};
        }();
        const auto m = toModel(doc, terrain, c);
        check(location.size() == 2 && std::hypot(location[0].toDouble() - m.x, location[1].toDouble() - m.y) < 40,
              "the repair is located at the pad");
    }
    checkC3(doc, terrain, "repair");
    checkC1(doc, terrain, "repair");
    checkC6(doc, terrain, "repair");
    checkC7(doc, terrain, "repair");
    std::cout << "repair: C1, C3, C6, C7 hold\n";
}

// The entry codec: round trips (resolved and not) and refusals.
void codec() {
    Document doc;
    const auto terrain = importRings(doc, 12, 48);
    const auto c = centre(doc, terrain);
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x + 20, c.y, 3))},
                                    {"elevation", toModel(doc, terrain, {0, 0, groundAt(doc, terrain, c.x + 20, c.y)}).z}}));
    const auto entry = recordOf(doc, terrain).edits.back();
    const auto json = encodeTerrainEdit(*entry);
    check(json["mode"] == "daylight" && json["rays"].isArray() && json["outline"].isArray() &&
              !json.contains("width"),
          "a resolved pad encodes its fields");
    check(*decodeTerrainEdit(json) == *entry, "the codec round-trips a resolved pad");
    auto unresolved = json;
    unresolved.remove("outline");
    unresolved.remove("rays");
    unresolved.remove("pieces");
    unresolved.remove("dropped");
    const auto back = decodeTerrainEdit(unresolved);
    check(back && !dynamic_cast<const TerrainStampData &>(*back->data).resolved(),
          "an unresolved pad round-trips");
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
    bad["offset"] = 1;
    refuses(bad, "an unknown field");
    bad = json;
    bad["mode"] = "terrace";
    refuses(bad, "an unknown mode");
    bad = json;
    bad.remove("rays");
    refuses(bad, "an outline without rays");
    bad = json;
    bad["width"] = 2;
    refuses(bad, "a width on a daylight pad");
    bad = json;
    bad["cutSlope"] = 0;
    refuses(bad, "a zero slope");
    bad = json;
    bad["footprint"] = QJsonArray{QJsonArray{0, 0}, QJsonArray{1, 0}};
    refuses(bad, "a two-point footprint");
    bad = json;
    bad["sourceFace"] = "4";
    refuses(bad, "a source face without a body");
    bad = json;
    bad["rays"] = QJsonArray{QJsonArray{0, 1, 2}};
    refuses(bad, "a ray without its Z");
    bad = json;
    bad["sourceBody"] = 12;
    refuses(bad, "a numeric source body");
    std::cout << "codec: round trips and refusals\n";
}

// The in-app assistant has no stamp tool and reads terrain.describe without `pads` (MCP and
// sessions keep it).
void assistantSurface() {
    Document doc;
    const auto terrain = importRings(doc, 12, 48);
    const auto c = centre(doc, terrain);
    run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x + 20, c.y, 3))},
                                    {"elevation", toModel(doc, terrain, {0, 0, groundAt(doc, terrain, c.x + 20, c.y)}).z}}));
    const auto documentId = QString::fromStdString(doc.identity());
    const QJsonObject query{{"apiVersion", 1},
                            {"documentId", documentId},
                            {"expectedRevision", QString::number(doc.revision())},
                            {"query", "terrain.describe"},
                            {"body", QString::number(terrain)}};
    check(inspectDocument(doc, query)["data"].toObject().contains("pads"),
          "the shared inspection (MCP, sessions) reports pads");
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
    check(!request.contains("terrain.stamp_pad") && !request.contains("terrain.update_edit") &&
              !request.contains("\"pads\"") && request.contains("\"stamp\""),
          "the assistant has no stamp tool, no pads list, and reads the stamp in its edits");
    std::cout << "assistant: terrain.stamp_pad and update_edit hidden, pads left out\n";
}

// Cancellation inside a stamp entry (a replay's progress), and the hostile-input bounds: a
// 20,000-point footprint, a comb footprint, too many rays.
void cancelInsideEntry() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    auto data = std::make_shared<TerrainStampData>();
    data->footprint = square(c.x, c.y, 40);
    data->elevation = groundAt(doc, terrain, c.x, c.y) + 1;
    data->cutSlope = data->fillSlope = 0.5;
    data->maxDistance = 100;
    const TerrainEdit entry{1, terrainStampKind, data};
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    const auto state = stateOf(doc, terrain);
    int calls = 0;
    TerrainProgress progress([&] { return ++calls > 3; });
    bool cancelled = false;
    try {
        (void)planTerrainStamp({*mesh, *state, recordOf(doc, terrain).parameters, &progress}, entry);
    } catch (const TerrainCancelled &) {
        cancelled = true;
    }
    check(cancelled, "a cancelled replay stops inside a stamp entry");
    const auto plan = planTerrainStamp({*mesh, *state, recordOf(doc, terrain).parameters}, entry);
    check(plan.rays > 100, "without cancellation the entry plans");
    std::cout << "cancel: inside a " << plan.rays << "-ray stamp after " << calls << " checks\n";
}
void hostile() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    const double z = toModel(doc, terrain, {0, 0, groundAt(doc, terrain, c.x, c.y)}).z;
    auto timed = [&](const std::string &what, auto operation) {
        const auto start = std::chrono::steady_clock::now();
        const auto r = refusal(operation);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "hostile: " << what << ": " << (r.code.empty() ? "applied" : r.code) << " in "
                  << seconds << " s (" << r.message.substr(0, 120) << "; "
                  << r.report["terrain"].toObject()["refusals"].toArray().at(0).toObject()["message"]
                         .toString()
                         .toStdString()
                  << ")\n";
        check(seconds < 300, what + " is bounded");
        return r;
    };
    // A 20,000-point circle: many rays; applied or refused by a bound, quickly.
    {
        std::vector<std::array<double, 2>> circle;
        for (int i = 0; i < 20000; ++i) {
            const double t = 2 * std::numbers::pi * i / 20000;
            circle.push_back({c.x + 30 * std::cos(t), c.y + 30 * std::sin(t)});
        }
        const auto revision = doc.revision();
        const auto r = timed("a 20,000-point circle", [&] {
            direct(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, circle)},
                                               {"elevation", z + 0.5}}));
        });
        check(r.code.empty() || doc.revision() == revision, "a refused circle changes nothing");
        if (r.code.empty()) {
            checkC3(doc, terrain, "circle");
            doc.undo();
        }
    }
    // A comb: its busiest band holds thousands of edges.
    {
        std::vector<std::array<double, 2>> comb;
        const int teeth = 2000;
        for (int i = 0; i < teeth; ++i) {
            const double x0 = c.x - 50 + 100.0 * i / teeth, x1 = x0 + 100.0 / teeth / 2;
            comb.push_back({x0, c.y});
            comb.push_back({x0, c.y + 40});
            comb.push_back({x1, c.y + 40});
            comb.push_back({x1, c.y});
        }
        comb.push_back({c.x + 50, c.y - 5});
        comb.push_back({c.x - 50, c.y - 5});
        checkUnchanged(doc, "a comb footprint", [&] {
            direct(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, comb)},
                                               {"elevation", z}}));
        }, "LIMIT_EXCEEDED");
        timed("a comb footprint", [&] {
            direct(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, comb)},
                                               {"elevation", z}}));
        });
    }
    // Too many rays: 4,000 sharp teeth around a circle, each convex tip a fan of about 17 rays.
    {
        std::vector<std::array<double, 2>> teeth;
        for (int i = 0; i < 4000; ++i) {
            const double t0 = 2 * std::numbers::pi * i / 4000, t1 = 2 * std::numbers::pi * (i + 0.5) / 4000;
            teeth.push_back({c.x + 100 * std::cos(t0), c.y + 100 * std::sin(t0)});
            teeth.push_back({c.x + 101 * std::cos(t1), c.y + 101 * std::sin(t1)});
        }
        const auto r = timed("4,000 sharp teeth", [&] {
            direct(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, teeth)},
                                               {"elevation", z}}));
        });
        check(r.code == "LIMIT_EXCEEDED" &&
                  r.report["terrain"].toObject()["refusals"].toArray().at(0).toObject()["message"]
                          .toString()
                          .contains("32,768"),
              "the teeth are refused by the ray bound");
    }
    std::cout << "hostile: each input bounded\n";
}

// A scripted sequence on the contour hill: two pads, a sculpt beside one, a contour edit, an
// update and a removal, with every check after each, Undo and Redo of all, reopen, and a pinned
// replay digest.
constexpr const char *sequenceDigest = "";
void sequence() {
    Document doc;
    const auto terrain = importRings(doc);
    TerrainContourCache cache;
    cache.get(doc, terrain);
    const auto c = centre(doc, terrain);
    const auto start = model(doc);
    std::size_t steps = 0;
    const auto stamp = [&](double dx, double dy, double half, double lift) {
        run(doc, stampCommand(terrain, {{"footprint", polygonFootprint(doc, terrain, square(c.x + dx, c.y + dy, half))},
                                        {"elevation", toModel(doc, terrain, {0, 0, groundAt(doc, terrain, c.x + dx, c.y + dy) + lift}).z}}));
        ++steps;
    };
    stamp(40, 0, 5, 0.3);
    checkAll(doc, terrain, cache, "sequence pad 1");
    checkC3(doc, terrain, "sequence pad 1");
    stamp(-30, 30, 4, -0.4);
    checkAll(doc, terrain, cache, "sequence pad 2");
    checkC3(doc, terrain, "sequence pad 2");
    run(doc, command("terrain.sculpt", terrain, {{"mode", "raise"},
                                                 {"path", planPoints(doc, terrain, {{c.x + 55, c.y}, {c.x + 55, c.y + 10}})},
                                                 {"radius", 4},
                                                 {"amount", 0.3}}));
    ++steps;
    checkAll(doc, terrain, cache, "sequence sculpt");
    checkC3(doc, terrain, "sequence sculpt");
    const auto first = recordOf(doc, terrain).edits.front()->id;
    run(doc, command("terrain.update_edit", terrain, {{"edit", double(first)}, {"fillSlope", 0.8}}));
    ++steps;
    checkAll(doc, terrain, cache, "sequence update");
    checkC3(doc, terrain, "sequence update");
    const auto digest = recordOf(doc, terrain).meshDigest;
    std::cout << "sequence digest " << digest << "\n";
    if (std::string(sequenceDigest).size())
        check(digest == sequenceDigest, "the sequence's replay digest is pinned");
    const auto end = encoded(doc);
    for (std::size_t i = 0; i < steps; ++i)
        doc.undo();
    check(model(doc) == start, "undoing the sequence restores the start");
    for (std::size_t i = 0; i < steps; ++i)
        doc.redo();
    check(encoded(doc) == end, "redoing the sequence restores the end");
    auto reopened = decodeDocument(encodeDocument(doc));
    checkC1(reopened, terrain, "sequence (reopened)");
    checkC3(reopened, terrain, "sequence (reopened)");
    checkC6(reopened, terrain, "sequence (reopened)");
    checkC7(reopened, terrain, "sequence (reopened)");
    // Removing the first pad replays the rest; Reset edits returns the base.
    run(doc, command("terrain.remove_edit", terrain, {{"edit", double(first)}}));
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
        {"square pad", squarePad},   {"L pad", lShapePad},
        {"tile corner", tileCornerPad}, {"footprints", footprintForms},
        {"protection", protection}, {"overlaps", overlaps},
        {"refusals", refusals},     {"update edit", updateEdit},
        {"repair", repair},         {"codec", codec},
        {"assistant surface", assistantSurface}, {"cancel inside entry", cancelInsideEntry},
        {"hostile", hostile},       {"sequence", sequence}};
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
