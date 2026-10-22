// R096.b (ADR 0169 §4.1, §6, §8, §9): sculpt and smooth. terrain.sculpt as the edit kinds
// `sculpt` and `smooth` that replay exactly: brush and region scope, the falloffs and their
// edges, the swept stroke (independent of sampling), Jacobi smoothing that reads and never writes
// the neighbours outside the influence, pad protection through the `protects` hook, contour
// clipping (decision 2) into the current sources, the parameter bounds as refusals that change
// nothing, and checks C1, C2, C5, C6 and C7 after the edits, after Undo and Redo, after save and
// reopen and after Regenerate. The helpers are R096.c's (terrain_source_edit_tests.cpp).
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
#include <future>
#include <iostream>
#include <map>
#include <numbers>
#include <set>
#include <thread>
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

// ---- Sculpt helpers --------------------------------------------------------------------------
QJsonObject sculptCommand(Id terrain, QJsonObject fields) {
    return command("terrain.sculpt", terrain, std::move(fields));
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
const TerrainSculptData &lastSculpt(const Document &doc, Id terrain) {
    return dynamic_cast<const TerrainSculptData &>(*recordOf(doc, terrain).edits.back()->data);
}
// Every terrain vertex's Z by exact plan position (copies agree by C6).
std::map<TerrainPlanKey, double> heights(const Document &doc, Id terrain) {
    std::map<TerrainPlanKey, double> result;
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        for (const auto &[vid, p] : tile->surface.vertices)
            result[TerrainPlanKey::of(p.x, p.y)] = p.z;
    return result;
}
std::map<TerrainPlanKey, Vec3> positions(const Document &doc, Id terrain) {
    std::map<TerrainPlanKey, Vec3> result;
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        for (const auto &[vid, p] : tile->surface.vertices)
            result[TerrainPlanKey::of(p.x, p.y)] = p;
    return result;
}
// The plan triangulation: every triangle as its three corners' plan positions, rotated to start
// at the smallest (orientation kept), sorted.
std::vector<std::array<TerrainPlanKey, 3>> planTriangles(const Document &doc, Id terrain) {
    std::vector<std::array<TerrainPlanKey, 3>> result;
    for (const auto &[id, tile] : tilesOf(doc, terrain)) {
        const auto content = terrainTileContent(*tile);
        for (const auto &t : content->triangles) {
            std::array<TerrainPlanKey, 3> k{};
            for (int i = 0; i < 3; ++i) {
                const auto &p = content->vertices[t[std::size_t(i)]];
                k[std::size_t(i)] = TerrainPlanKey::of(p.x, p.y);
            }
            std::rotate(k.begin(), std::min_element(k.begin(), k.end()), k.end());
            result.push_back(k);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}
// The change of the last edit agrees with its weights: every vertex with weight 0 (or on a pad)
// is unchanged, and for a raise every other moved by exactly amount * w. Returns the count moved.
std::size_t checkRaise(const std::map<TerrainPlanKey, Vec3> &before,
                       const std::map<TerrainPlanKey, double> &after,
                       const TerrainSculptData &data, const std::string &when,
                       const TerrainPlanPolygon *pad = nullptr) {
    const TerrainSculptInfluence influence(data);
    check(before.size() == after.size(), when + ": no vertex is added or removed");
    std::size_t moved = 0;
    for (const auto &[key, p] : before) {
        const double z = after.at(key);
        double w = influence.weight(p.x, p.y);
        if (pad) {
            int winding = 0;
            bool on = false;
            for (std::size_t i = 0; i < pad->size(); ++i) {
                const auto &a = (*pad)[i], &b = (*pad)[(i + 1) % pad->size()];
                const int o = terrainOrientation({a[0], a[1]}, {b[0], b[1]}, {p.x, p.y});
                if (o == 0 && std::min(a[0], b[0]) <= p.x && p.x <= std::max(a[0], b[0]) &&
                    std::min(a[1], b[1]) <= p.y && p.y <= std::max(a[1], b[1]))
                    on = true;
                if (a[1] <= p.y) {
                    if (b[1] > p.y && o > 0)
                        ++winding;
                } else if (b[1] <= p.y && o < 0)
                    --winding;
            }
            if (on || winding)
                w = 0;
        }
        if (!(w > 0)) {
            check(z == p.z, when + ": a vertex outside the influence (or on a pad) is unchanged");
            continue;
        }
        if (data.amount != 0)
            check(z == p.z + data.amount * w, when + ": a vertex moves by amount * w exactly");
        moved += z != p.z;
    }
    return moved;
}
// Decision 2: no current contour, breakline or spot-point vertex lies where the last edit has a
// weight (outside pads), and every current source vertex was a source vertex before.
void checkClipped(const Document &doc, Id terrain, const TerrainSourceState &before,
                  const std::string &when, const TerrainPlanPolygon *pad = nullptr) {
    const TerrainSculptInfluence influence(lastSculpt(doc, terrain));
    std::set<TerrainPlanKey> was;
    for (const auto *source : before.sources())
        for (const auto &p : source->points)
            was.insert(TerrainPlanKey::of(p.x, p.y));
    const auto state = stateOf(doc, terrain);
    for (const auto *source : state->sources()) {
        if (source->role == TerrainSourceRole::Boundary || source->role == TerrainSourceRole::Hole)
            continue;
        for (const auto &p : source->points) {
            check(was.contains(TerrainPlanKey::of(p.x, p.y)),
                  when + ": the current sources hold only former source vertices");
            if (!pad)
                check(!(influence.weight(p.x, p.y) > 0),
                      when + ": no current source vertex lies under the change (decision 2)");
        }
    }
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

// ---- Tests -----------------------------------------------------------------------------------
// The weight function: falloff values and edges, sampling, reversal, and the region index
// against a brute-force reference.
void weights() {
    check(terrainFalloff(TerrainFalloff::Smooth, 0) == 1 &&
              terrainFalloff(TerrainFalloff::Smooth, 1) == 0 &&
              terrainFalloff(TerrainFalloff::Smooth, 0.5) == 0.5 &&
              terrainFalloff(TerrainFalloff::Linear, 0.25) == 0.75 &&
              terrainFalloff(TerrainFalloff::Constant, 0.999) == 1 &&
              terrainFalloff(TerrainFalloff::Constant, 1) == 0 &&
              terrainFalloff(TerrainFalloff::Linear, 1.5) == 0,
          "the falloffs are 1 at the path and 0 at the radius");
    for (auto falloff : {TerrainFalloff::Smooth, TerrainFalloff::Linear})
        for (double t = 0; t < 1; t += 1.0 / 64)
            check(terrainFalloff(falloff, t) >= terrainFalloff(falloff, t + 1.0 / 64),
                  "the falloff never rises");
    TerrainSculptData click;
    click.path = {{0, 0}};
    click.radius = 2;
    for (auto falloff : {TerrainFalloff::Smooth, TerrainFalloff::Linear, TerrainFalloff::Constant}) {
        click.falloff = falloff;
        const TerrainSculptInfluence brush(click);
        check(brush.weight(0, 0) == 1, "a click weighs 1 at its point");
        check(brush.weight(2, 0) == 0 && brush.weight(0, -2) == 0 && brush.weight(3, 1) == 0,
              "a vertex exactly at the radius or beyond does not move");
        check(brush.weight(2 - 1e-6, 0) > 0, "a vertex just inside the radius moves");
        if (falloff != TerrainFalloff::Smooth)
            check(brush.weight(std::nextafter(2.0, 0.0), 0) > 0,
                  "a vertex one ulp inside the radius moves");
    }
    click.falloff = TerrainFalloff::Linear;
    check(TerrainSculptInfluence(click).weight(1, 0) == 0.5, "linear is 0.5 halfway");
    // A swept path: its reverse and repeated samples weigh alike bit for bit; collinear
    // resampling within 1e-12.
    TerrainSculptData stroke;
    stroke.radius = 7;
    stroke.path = {{0, 0}, {10, 3}, {20, -4}, {26, 9}};
    auto reversed = stroke, repeated = stroke, resampled = stroke;
    std::reverse(reversed.path.begin(), reversed.path.end());
    repeated.path = {{0, 0}, {0, 0}, {10, 3}, {10, 3}, {10, 3}, {20, -4}, {26, 9}, {26, 9}};
    resampled.path = {{0, 0}, {5, 1.5}, {10, 3}, {15, -0.5}, {20, -4}, {23, 2.5}, {26, 9}};
    const TerrainSculptInfluence a(stroke), b(reversed), c(repeated), d(resampled);
    std::mt19937_64 random(96);
    std::uniform_real_distribution<double> x(-10, 36), y(-14, 19);
    std::size_t inside = 0;
    for (int i = 0; i < 20000; ++i) {
        const double px = x(random), py = y(random);
        const double w = a.weight(px, py);
        inside += w > 0;
        check(w == b.weight(px, py) && w == c.weight(px, py),
              "a reversed path or repeated samples weigh alike bit for bit");
        check(std::abs(w - d.weight(px, py)) <= 1e-12, "collinear resampling weighs alike");
    }
    check(inside > 1000, "the stroke weighs many positions");
    // A region with a falloff width.
    TerrainSculptData square;
    square.region = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    square.width = 2;
    square.falloff = TerrainFalloff::Linear;
    {
        const TerrainSculptInfluence region(square);
        check(region.weight(2, 2) == 1 && region.weight(4, 2) == 1 && region.weight(0, 0) == 1,
              "a region weighs 1 inside and on its outline");
        check(region.weight(5, 2) == 0.5 && region.weight(6, 2) == 0 && region.weight(7, 2) == 0,
              "outside a region the weight falls to 0 at the width");
    }
    square.width = 0;
    check(TerrainSculptInfluence(square).weight(std::nextafter(4.0, 5.0), 2) == 0,
          "a region without a falloff width weighs 0 just outside");
    // A 20,000-corner star against brute force: the band and grid indexes find every edge.
    TerrainSculptData star;
    star.width = 0.75;
    const int corners = 20000;
    for (int i = 0; i < corners; ++i) {
        const double t = 2 * std::numbers::pi * i / corners;
        const double r = 40 + 15 * std::sin(37 * t) + 3 * std::cos(611 * t);
        star.region.push_back({r * std::cos(t), r * std::sin(t)});
    }
    validateTerrainSculptData(terrainSmoothKind, [&] {
        auto s = star;
        s.strength = 1;
        s.iterations = 1;
        return s;
    }());
    const TerrainSculptInfluence index(star);
    std::uniform_real_distribution<double> u(-60, 60);
    std::size_t in = 0, near = 0;
    // 600 probes: about 225 inside the star (37% of the square), against the > 150 below.
    for (int i = 0; i < 600; ++i) {
        const double px = u(random), py = u(random);
        int winding = 0;
        double best = 1e300;
        for (int k = 0; k < corners; ++k) {
            const auto &p = star.region[std::size_t(k)], &q = star.region[std::size_t((k + 1) % corners)];
            const int o = terrainOrientation({p[0], p[1]}, {q[0], q[1]}, {px, py});
            if (p[1] <= py) {
                if (q[1] > py && o > 0)
                    ++winding;
            } else if (q[1] <= py && o < 0)
                --winding;
            const auto &lo = std::tie(p[0], p[1]) < std::tie(q[0], q[1]) ? p : q;
            const auto &hi = &lo == &p ? q : p;
            const double dx = hi[0] - lo[0], dy = hi[1] - lo[1], len = dx * dx + dy * dy;
            const double t = ((px - lo[0]) * dx + (py - lo[1]) * dy) / len;
            const double dist = t <= 0   ? std::sqrt((px - lo[0]) * (px - lo[0]) + (py - lo[1]) * (py - lo[1]))
                                : t >= 1 ? std::sqrt((px - hi[0]) * (px - hi[0]) + (py - hi[1]) * (py - hi[1]))
                                         : std::sqrt((lo[0] + t * dx - px) * (lo[0] + t * dx - px) +
                                                     (lo[1] + t * dy - py) * (lo[1] + t * dy - py));
            best = std::min(best, dist);
        }
        const bool expected = winding != 0;
        check(index.inside(px, py) == expected, "the band index gives the winding test");
        in += expected;
        if (!expected && best < star.width) {
            ++near;
            // Within an ulp or two: this file is not built with -ffp-contract=off.
            check(std::abs(index.weight(px, py) - terrainFalloff(star.falloff, best / star.width)) <=
                      1e-14,
                  "the edge grid finds the nearest edge");
        }
        if (!expected && !(best < star.width))
            check(index.weight(px, py) == 0, "beyond the width a region weighs 0");
    }
    check(in > 150 && near > 10, "the star test reached inside and the falloff band");
    std::cout << "weights: falloff edges, sampling, reversal and a 20,000-corner region ("
              << in << " inside, " << near << " in the band)\n";
}

// A raise along a stroke across the hill: each vertex moves by amount * w, the plan is
// unchanged, crossed rings are clipped into runs (decision 2), the preview equals the commit,
// and C1, C2, C5, C6 and C7 hold, also after Undo, Redo, save and reopen.
void raiseBrush() {
    Document doc;
    const auto terrain = importRings(doc);
    {
        auto display = recordOf(doc, terrain).display;
        display.contourInterval = 0.5;
        setTerrainDisplay(doc, terrain, display);
    }
    TerrainContourCache cache;
    (void)cache.get(doc, terrain);
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const auto vertices = positions(doc, terrain);
    const auto plan = planTriangles(doc, terrain);
    const auto sourcesBefore = *stateOf(doc, terrain);
    const auto path = relPlan(doc, terrain, {{-20, 0}, {-6, 1}, {20, 0}});
    const auto operation = run(doc, sculptCommand(terrain, {{"mode", "raise"},
                                                            {"path", planPoints(doc, terrain, path)},
                                                            {"radius", 4},
                                                            {"amount", 1.5}}));
    check(operation["edit"] == 1 && operation["mode"] == "raise", "the sculpt names its entry");
    const auto &record = recordOf(doc, terrain);
    check(record.edits.size() == 1 && record.edits[0]->kind == terrainSculptKind,
          "the edit is recorded as sculpt");
    const auto &data = lastSculpt(doc, terrain);
    const auto moved = checkRaise(vertices, heights(doc, terrain), data, "raise");
    check(moved > 20, "the stroke moves vertices");
    check(planTriangles(doc, terrain) == plan, "only Z changes: the plan triangulation is kept");
    const auto report = operation["terrainReport"].toObject()["terrain"].toObject();
    check(report["verticesMoved"] == double(moved) && operation["verticesMoved"] == double(moved),
          "the report counts the vertices moved");
    const auto replaced = checkC5(doc, terrain, snap, "raise");
    check(report["tilesTouched"] == double(replaced), "the report counts the tiles touched");
    // Decision 2: the crossed rings are clipped; a ring crossed twice splits into two runs, the
    // second with a split id.
    checkClipped(doc, terrain, sourcesBefore, "raise");
    const auto clipped = report["sourcesClipped"].toObject();
    check(clipped["sources"].toDouble() >= 3 && clipped["pieces"].toDouble() >= 3 &&
              clipped["vertices"].toDouble() > 0,
          "the report counts the clipped sources, vertices and pieces");
    const auto state = stateOf(doc, terrain);
    const auto piece = state->find(*terrainSplitSourceId(1, 0));
    check(piece && piece->role == TerrainSourceRole::Contour && piece->points.size() >= 2 &&
              piece->points.front() != piece->points.back(),
          "a further run is a new open contour with a split id");
    check(state->size() == sourcesBefore.size() + std::size_t(clipped["pieces"].toDouble()) -
                               std::size_t(clipped["removed"].toDouble()),
          "the current sources gain the pieces");
    const auto description = terrainDescription(doc, terrain);
    check(description["edits"].toObject()["entries"].toArray()[0].toObject()["kind"] == "sculpt",
          "terrain.describe lists the sculpt");
    // The preview of the same entry equals the commit bit for bit.
    {
        auto entry = std::make_shared<TerrainSculptData>(data);
        doc.undo();
        const auto preview = previewTerrainEdit(doc, terrain, terrainSculptKind, entry);
        doc.redo();
        check(preview.liftedVertices == moved, "the preview lifts the vertices the commit moves");
        std::map<TerrainTileKey, TerrainTileContent> committed;
        for (const auto &[id, tile] : tilesOf(doc, terrain))
            committed[tile->tileKey] = *terrainTileContent(*tile);
        for (const auto &tile : preview.region.tiles)
            check(committed.contains(tile->key) && committed.at(tile->key).sameMesh(*tile),
                  "the committed tiles equal the previewed candidate");
    }
    checkAll(doc, terrain, cache, "raise");
    {
        // Undo restores the tiles pointer-identical.
        const auto edited = tilesOf(doc, terrain);
        doc.undo();
        check(tilesOf(doc, terrain) == snap.tiles, "Undo restores the tiles pointer-identical");
        doc.redo();
        check(tilesOf(doc, terrain) == edited, "Redo restores the edited tiles");
    }
    checkHistoryAndSave(doc, terrain, before, "raise");
    // Determinism: the same command on a second document gives an identical document.
    Document other;
    const auto copy = importRings(other);
    {
        auto display = recordOf(other, copy).display;
        display.contourInterval = 0.5;
        setTerrainDisplay(other, copy, display);
    }
    run(other, sculptCommand(copy, {{"mode", "raise"},
                                    {"path", planPoints(other, copy, path)},
                                    {"radius", 4},
                                    {"amount", 1.5}}));
    check(model(other) == model(doc), "the same sculpt gives identical documents");
    std::cout << "raise: " << moved << " vertices moved, " << replaced << " tiles touched, "
              << clipped["vertices"].toDouble() << " source vertices clipped into "
              << clipped["pieces"].toDouble() << " further pieces\n";
}

// Smoothing a region with a falloff on the point terrain: the Jacobi result (checked against an
// independent long-double computation), neighbours outside read and never written, spot points
// clipped, and the checks.
void smoothRegion() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto before = model(doc);
    const auto snap = snapshot(doc, terrain);
    const auto vertices = positions(doc, terrain);
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true); // the mesh before
    const auto pointsBefore = stateOf(doc, terrain)->vertexCount();
    const auto sourcesBefore = *stateOf(doc, terrain);
    const auto region = relPlan(doc, terrain, {{-30, -20}, {25, -25}, {35, 10}, {0, 30}, {-28, 12}});
    const auto operation =
        run(doc, sculptCommand(terrain, {{"mode", "smooth"},
                                         {"region", planPoints(doc, terrain, region)},
                                         {"falloffWidth", 6},
                                         {"falloff", "linear"},
                                         {"strength", 0.8},
                                         {"iterations", 3}}));
    const auto &data = lastSculpt(doc, terrain);
    check(recordOf(doc, terrain).edits.back()->kind == terrainSmoothKind &&
              data.iterations == 3 && data.strength == 0.8 && !data.brush(),
          "the edit is recorded as smooth");
    const auto after = heights(doc, terrain);
    // The reference: Jacobi in long double over the weighted vertices.
    const TerrainSculptInfluence influence(data);
    std::map<TerrainPlanKey, long double> z;
    std::map<TerrainPlanKey, double> w;
    std::map<TerrainPlanKey, std::vector<std::pair<TerrainPlanKey, long double>>> ring;
    for (const auto &[key, p] : vertices) {
        z[key] = p.z;
        if (const double weight = influence.weight(p.x, p.y); weight > 0)
            w[key] = weight;
    }
    for (const auto &[key, weight] : w) {
        const auto &p = vertices.at(key);
        std::set<TerrainPlanKey> seen;
        for (const auto &ref : mesh->trianglesAt(p)) {
            const auto &content = *mesh->tile(ref.tile)->content;
            for (const auto corner : content.triangles[ref.triangle]) {
                const auto &q = content.vertices[corner];
                const auto k = TerrainPlanKey::of(q.x, q.y);
                if (k != key && seen.insert(k).second)
                    ring[key].push_back({k, 1 / std::sqrt((long double)(q.x - p.x) * (q.x - p.x) +
                                                          (long double)(q.y - p.y) * (q.y - p.y))});
            }
        }
    }
    for (int iteration = 0; iteration < 3; ++iteration) {
        auto next = z;
        for (const auto &[key, weight] : w) {
            long double sum = 0, weights = 0;
            for (const auto &[k, iw] : ring[key]) {
                sum += iw * z[k];
                weights += iw;
            }
            next[key] = z[key] + 0.8L * weight * (sum / weights - z[key]);
        }
        z = std::move(next);
    }
    std::size_t moved = 0;
    for (const auto &[key, p] : vertices) {
        const double now = after.at(key);
        if (!w.contains(key)) {
            check(now == p.z, "smooth: a neighbour outside the influence is read, never written");
            continue;
        }
        check(std::abs((long double)now - z[key]) <= 1e-9L, "smooth: the Jacobi result");
        moved += now != p.z;
    }
    check(moved > 100, "smoothing moves vertices");
    check(operation["verticesMoved"] == double(moved), "the report counts the vertices moved");
    // Spot points under the region are clipped (decision 2).
    checkClipped(doc, terrain, sourcesBefore, "smooth");
    const auto clipped = operation["terrainReport"].toObject()["terrain"].toObject()["sourcesClipped"].toObject();
    check(stateOf(doc, terrain)->vertexCount() == pointsBefore - std::size_t(clipped["vertices"].toDouble()) &&
              clipped["vertices"].toDouble() >= double(moved),
          "the clipped spot points leave the current sources");
    const auto replaced = checkC5(doc, terrain, snap, "smooth");
    checkC1(doc, terrain, "smooth");
    checkC6(doc, terrain, "smooth");
    checkC7(doc, terrain, "smooth");
    checkHistoryAndSave(doc, terrain, before, "smooth");
    // A wide brush touching many tiles: seams hold across all of them.
    const auto snapWide = snapshot(doc, terrain);
    const auto wide = run(doc, sculptCommand(terrain,
                                             {{"mode", "smooth"},
                                              {"path", planPoints(doc, terrain,
                                                                  relPlan(doc, terrain, {{60, -40}, {90, 20}}))},
                                              {"radius", 45},
                                              {"iterations", 2}}));
    const auto touched = wide["terrainReport"].toObject()["terrain"].toObject()["tilesTouched"].toDouble();
    check(touched >= 4, "a wide brush touches several tiles");
    checkC5(doc, terrain, snapWide, "wide smooth");
    checkC1(doc, terrain, "wide smooth");
    checkC6(doc, terrain, "wide smooth");
    checkC7(doc, terrain, "wide smooth");
    std::cout << "smooth: " << moved << " vertices moved, " << replaced << " tiles touched; wide "
              << "brush " << touched << " tiles\n";
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
void falloffEdges() {
    Document doc;
    const auto terrain = importGrid(doc);
    const auto vertices = positions(doc, terrain);
    // The grid vertex nearest the centre, and its neighbours by offset.
    const auto c = centre(doc, terrain);
    Vec3 origin{};
    double best = 1e300;
    for (const auto &[key, p] : vertices)
        if (const double d = std::abs(p.x - c.x) + std::abs(p.y - c.y); d < best) {
            best = d;
            origin = p;
        }
    auto at = [&](double dx, double dy) {
        return vertices.at(TerrainPlanKey::of(origin.x + dx, origin.y + dy));
    };
    auto rise = [&](double dx, double dy) {
        const auto p = at(dx, dy);
        return heights(doc, terrain).at(TerrainPlanKey::of(p.x, p.y)) - p.z;
    };
    auto exact = [&](double dx, double dy, double expected, const std::string &what) {
        const auto p = at(dx, dy);
        check(heights(doc, terrain).at(TerrainPlanKey::of(p.x, p.y)) == p.z + expected, what);
    };
    const auto click = planPoints(doc, terrain, {{origin.x, origin.y}});
    // Linear, radius 2.
    run(doc, sculptCommand(terrain, {{"mode", "raise"}, {"path", click}, {"radius", 2},
                                     {"falloff", "linear"}, {"amount", 1}}));
    check(lastSculpt(doc, terrain).path[0][0] == origin.x &&
              lastSculpt(doc, terrain).path[0][1] == origin.y,
          "the click lands exactly on the grid vertex");
    exact(0, 0, 1, "linear: the centre moves by the amount");
    exact(1, 0, 0.5, "linear: halfway moves by half");
    exact(1, 1, terrainFalloff(TerrainFalloff::Linear, std::sqrt(2.0) / 2),
          "linear: the diagonal moves by f(d / r)");
    check(rise(2, 0) == 0 && rise(0, -2) == 0 && rise(2, 1) == 0,
          "linear: a vertex at the radius or beyond stays");
    doc.undo();
    // Constant, radius 2: everything strictly inside moves fully.
    run(doc, sculptCommand(terrain, {{"mode", "raise"}, {"path", click}, {"radius", 2},
                                     {"falloff", "constant"}, {"amount", -0.75}}));
    exact(1, 1, -0.75, "constant: inside moves fully (negative lowers)");
    check(rise(2, 0) == 0 && rise(-2, 0) == 0, "constant: a vertex at the radius stays");
    doc.undo();
    // Smooth falloff halfway is 0.5.
    run(doc, sculptCommand(terrain, {{"mode", "raise"}, {"path", click}, {"radius", 2},
                                     {"amount", 2}}));
    exact(1, 0, 1, "smooth: halfway moves by half");
    check(rise(0, 2) == 0, "smooth: a vertex at the radius stays");
    doc.undo();
    // A region square around the centre: the outline is inside; the width falls off.
    const auto square = planPoints(doc, terrain, {{origin.x - 1, origin.y - 1}, {origin.x + 1, origin.y - 1},
                                                  {origin.x + 1, origin.y + 1}, {origin.x - 1, origin.y + 1}});
    run(doc, sculptCommand(terrain, {{"mode", "raise"}, {"region", square}, {"amount", 1}}));
    exact(1, 1, 1, "region: a corner of the outline moves fully");
    exact(0, 1, 1, "region: a vertex on an outline edge moves fully");
    check(rise(2, 0) == 0, "region: without a width the outside stays");
    doc.undo();
    run(doc, sculptCommand(terrain, {{"mode", "raise"}, {"region", square}, {"falloffWidth", 2},
                                     {"falloff", "linear"}, {"amount", 1}}));
    exact(2, 0, 0.5, "region: halfway across the width moves by half");
    check(rise(3, 0) == 0 && rise(0, -3) == 0, "region: a vertex at the width stays");
    checkC6(doc, terrain, "grid region");
    checkC7(doc, terrain, "grid region");
    std::cout << "falloff edges: linear, constant, smooth and region edges hold on the grid\n";
}

// Sampling and replay: a stroke sampled densely or collinearly gives the same surface (within
// 1e-9 m; bit for bit for a reversed or repeated path), never accumulates, and replays exactly.
void sampling() {
    auto surface = [](const std::vector<std::array<double, 2>> &local, double amount) {
        Document doc;
        const auto terrain = importRings(doc);
        auto path = relPlan(doc, terrain, local);
        run(doc, sculptCommand(terrain, {{"mode", "raise"},
                                         {"path", planPoints(doc, terrain, path)},
                                         {"radius", 5},
                                         {"amount", amount}}));
        checkC7(doc, terrain, "sampled stroke");
        return heights(doc, terrain);
    };
    Document base;
    const auto baseTerrain = importRings(base);
    const auto original = heights(base, baseTerrain);
    const auto two = surface({{-16, 2}, {16, 2}}, 1);
    std::vector<std::array<double, 2>> dense;
    for (int i = 0; i <= 255; ++i)
        dense.push_back({-16 + 32.0 * i / 255, 2});
    const auto many = surface(dense, 1);
    const auto reversed = surface({{16, 2}, {-16, 2}}, 1);
    const auto repeated = surface({{-16, 2}, {-16, 2}, {16, 2}, {16, 2}}, 1);
    check(reversed == two && repeated == two,
          "a reversed or repeated stroke gives the same surface bit for bit");
    double worst = 0, rise = 0;
    for (const auto &[key, z] : two) {
        worst = std::max(worst, std::abs(z - many.at(key)));
        rise = std::max(rise, many.at(key) - original.at(key));
    }
    check(worst <= 1e-9, "a densely sampled stroke gives the same surface within 1e-9 m");
    check(rise <= 1 + 1e-12 && rise > 0.5,
          "a swept stroke moves each vertex once (no accumulation over 256 samples)");
    std::cout << "sampling: dense vs two-point stroke differ by " << worst << " m at most\n";
}

// The parameter bounds and the other refusals change nothing.
void refusals() {
    Document doc;
    const auto terrain = importRings(doc);
    const auto c = relPlan(doc, terrain, {{13, 0}});
    const auto click = planPoints(doc, terrain, c);
    auto brush = [&](QJsonObject fields) {
        if (!fields.contains("mode"))
            fields["mode"] = "raise";
        if (!fields.contains("path") && !fields.contains("region"))
            fields["path"] = click;
        return sculptCommand(terrain, fields);
    };
    auto refused = [&](const std::string &what, QJsonObject fields, const char *code) {
        checkUnchanged(doc, what, [&] { direct(doc, brush(fields)); }, code);
    };
    refused("no path", {{"path", QJsonArray{}}, {"radius", 2}, {"amount", 1}}, "INVALID_REQUEST");
    QJsonArray long257;
    for (int i = 0; i < 257; ++i)
        long257.append(click[0]);
    refused("a 257-point path", {{"path", long257}, {"radius", 2}, {"amount", 1}}, "LIMIT_EXCEEDED");
    refused("a radius below 0.01 m", {{"radius", 0.009}, {"amount", 1}}, "INVALID_REQUEST");
    refused("a radius above 1,000 m", {{"radius", 1000.5}, {"amount", 1}}, "INVALID_REQUEST");
    refused("a missing radius", {{"amount", 1}}, "INVALID_REQUEST");
    refused("an unknown falloff", {{"radius", 2}, {"amount", 1}, {"falloff", "cubic"}}, "INVALID_REQUEST");
    refused("an amount of 0", {{"radius", 2}, {"amount", 0}}, "INVALID_REQUEST");
    refused("an amount above 1,000 m", {{"radius", 2}, {"amount", -1000.5}}, "INVALID_REQUEST");
    refused("a raise without an amount", {{"radius", 2}}, "INVALID_REQUEST");
    refused("a raise with a strength", {{"radius", 2}, {"amount", 1}, {"strength", 0.5}}, "INVALID_REQUEST");
    refused("a smooth with an amount", {{"mode", "smooth"}, {"radius", 2}, {"amount", 1}}, "INVALID_REQUEST");
    refused("a strength of 0", {{"mode", "smooth"}, {"radius", 2}, {"strength", 0}}, "INVALID_REQUEST");
    refused("a strength above 1", {{"mode", "smooth"}, {"radius", 2}, {"strength", 1.5}}, "INVALID_REQUEST");
    refused("0 iterations", {{"mode", "smooth"}, {"radius", 2}, {"iterations", 0}}, "INVALID_REQUEST");
    refused("9 iterations", {{"mode", "smooth"}, {"radius", 2}, {"iterations", 9}}, "INVALID_REQUEST");
    refused("fractional iterations", {{"mode", "smooth"}, {"radius", 2}, {"iterations", 1.5}}, "INVALID_REQUEST");
    refused("an unknown mode", {{"mode", "lower"}, {"radius", 2}, {"amount", 1}}, "INVALID_REQUEST");
    refused("an unknown field", {{"radius", 2}, {"amount", 1}, {"pressure", 1}}, "INVALID_REQUEST");
    const auto square = planPoints(doc, terrain, relPlan(doc, terrain, {{-3, -3}, {3, -3}, {3, 3}, {-3, 3}}));
    refused("a path and a region", {{"path", click}, {"region", square}, {"radius", 2}, {"amount", 1}},
            "INVALID_REQUEST");
    refused("a region with a radius", {{"region", square}, {"radius", 2}, {"amount", 1}},
            "INVALID_REQUEST");
    refused("a brush with a falloff width", {{"radius", 2}, {"falloffWidth", 1}, {"amount", 1}}, "INVALID_REQUEST");
    {
        QJsonObject fields{{"mode", "raise"}, {"amount", 1}};
        fields["region"] = planPoints(doc, terrain, relPlan(doc, terrain, {{0, 0}, {3, 0}}));
        checkUnchanged(doc, "a two-point region", [&] { direct(doc, sculptCommand(terrain, fields)); },
                       "INVALID_REQUEST");
        fields["region"] = planPoints(doc, terrain, relPlan(doc, terrain, {{0, 0}, {3, 0}, {6, 0}}));
        checkUnchanged(doc, "a region without area", [&] { direct(doc, sculptCommand(terrain, fields)); },
                       "INVALID_REQUEST");
        QJsonArray huge;
        for (int i = 0; i < 20002; ++i) {
            const double t = 2 * std::numbers::pi * i / 20002;
            huge.append(planPoints(doc, terrain, relPlan(doc, terrain, {{20 * std::cos(t), 20 * std::sin(t)}}))[0]);
        }
        fields["region"] = huge;
        checkUnchanged(doc, "a 20,002-corner region", [&] { direct(doc, sculptCommand(terrain, fields)); },
                       "LIMIT_EXCEEDED");
        fields["region"] = square;
        fields["falloffWidth"] = -1;
        checkUnchanged(doc, "a negative falloff width", [&] { direct(doc, sculptCommand(terrain, fields)); },
                       "INVALID_REQUEST");
        fields["falloffWidth"] = 1000.5;
        checkUnchanged(doc, "a falloff width above 1,000 m", [&] { direct(doc, sculptCommand(terrain, fields)); },
                       "INVALID_REQUEST");
    }
    // A brush between the vertices of the sparse hilltop moves nothing: refused with a report.
    const auto empty = refusal([&] {
        direct(doc, sculptCommand(terrain, {{"mode", "raise"},
                                            {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{0, 0}}))},
                                            {"radius", 0.5},
                                            {"amount", 1}}));
    });
    check(empty.code == "INVALID_REQUEST" && !empty.report.isEmpty() &&
              empty.message.find("no terrain vertex") != std::string::npos,
          "a brush that catches no vertex is refused with a located report");
    checkUnchanged(doc, "an empty brush", [&] {
        direct(doc, sculptCommand(terrain, {{"mode", "raise"},
                                            {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{0, 0}}))},
                                            {"radius", 0.5},
                                            {"amount", 1}}));
    }, "INVALID_REQUEST");
    checkUnchanged(doc, "a missing terrain", [&] {
        direct(doc, sculptCommand(terrain + 999, {{"mode", "raise"}, {"path", click}, {"radius", 2}, {"amount", 1}}));
    }, "NOT_FOUND");
    {
        auto body = std::make_shared<Body>(*doc.bodies().at(terrain));
        body->locked = true;
        doc.apply({"Lock", {{terrain, doc.bodies().at(terrain), body}}}, doc.revision());
        checkUnchanged(doc, "a locked terrain", [&] {
            direct(doc, sculptCommand(terrain, {{"mode", "raise"}, {"path", click}, {"radius", 2}, {"amount", 1}}));
        }, "INVALID_OPERATION");
        doc.undo();
    }
    // Through the batch API the refusal changes nothing either.
    checkUnchanged(doc, "a refused batch sculpt", [&] {
        const auto response = executeBatch(doc, batch(doc, {brush({{"radius", 0.001}, {"amount", 1}})}));
        if (response.contains("error"))
            throw TerrainError(response["error"].toObject()["code"].toString().toStdString(), "batch");
    }, "INVALID_REQUEST");
    std::cout << "refusals: the bounds and the other refusals change nothing\n";
}

// Pads (decision 6) through the `protects` hook: a test-only kind stands in for R096.d's stamp.
struct Protect final : TerrainEditData {
    TerrainPlanPolygon polygon;
    bool equals(const TerrainEditData &other) const override {
        const auto *o = dynamic_cast<const Protect *>(&other);
        return o && o->polygon == polygon;
    }
    std::size_t bytes() const override { return sizeof(Protect) + polygon.size() * 16; }
};
void registerProtect() {
    registerTerrainEditKind(
        {"test.protect",
         [](const TerrainEditMesh &, const TerrainEdit &) {
             return TerrainEditOutcome{TerrainRegionChange{}, {}};
         },
         [](const TerrainEdit &) { return std::array<double, 4>{0, 0, 0, 0}; }, nullptr, nullptr,
         [](const TerrainEdit &edit) {
             return std::vector<TerrainPlanPolygon>{dynamic_cast<const Protect &>(*edit.data).polygon};
         }});
    // Its codec, so documents holding it encode and reopen.
    registerTerrainEditCodec(
        {"test.protect",
         [](const TerrainEditData &data) {
             QJsonArray polygon;
             for (const auto &p : dynamic_cast<const Protect &>(data).polygon)
                 polygon.append(QJsonArray{p[0], p[1]});
             return QJsonObject{{"polygon", polygon}};
         },
         [](const QJsonObject &entry) -> std::shared_ptr<const TerrainEditData> {
             auto data = std::make_shared<Protect>();
             for (const auto &p : entry["polygon"].toArray())
                 data->polygon.push_back({p.toArray()[0].toDouble(), p.toArray()[1].toDouble()});
             return data;
         }});
}
void padProtection() {
    registerProtect();
    Document doc;
    const auto terrain = importRings(doc);
    auto pad = std::make_shared<Protect>();
    const auto c = centre(doc, terrain);
    // Over the east side of the innermost rings (x = 13 m is ring 0 on the axis).
    pad->polygon = {{c.x + 11, c.y - 3}, {c.x + 17, c.y - 3}, {c.x + 17, c.y + 3}, {c.x + 11, c.y + 3}};
    applyTerrainEdit(doc, terrain, "test.protect", pad, "Protect");
    {
        // Pads away from the brush, and a degenerate one, change nothing (the pad boxes are
        // computed once per plan; R096.d's pads come through the same hook).
        auto far = std::make_shared<Protect>();
        far->polygon = {{c.x - 40, c.y - 40}, {c.x - 30, c.y - 40}, {c.x - 30, c.y - 30}};
        applyTerrainEdit(doc, terrain, "test.protect", far, "Protect");
        auto line = std::make_shared<Protect>();
        line->polygon = {{c.x + 10, c.y}, {c.x + 20, c.y}};
        applyTerrainEdit(doc, terrain, "test.protect", line, "Protect");
    }
    const auto vertices = positions(doc, terrain);
    const auto sourcesBefore = *stateOf(doc, terrain);
    const auto operation =
        run(doc, sculptCommand(terrain, {{"mode", "raise"},
                                         {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{14, 0}}))},
                                         {"radius", 7},
                                         {"amount", 1}}));
    const auto report = operation["terrainReport"].toObject()["terrain"].toObject();
    check(report["verticesProtected"].toDouble() > 0, "the report counts the protected vertices");
    const auto moved = checkRaise(vertices, heights(doc, terrain), lastSculpt(doc, terrain),
                                  "pad", &pad->polygon);
    check(moved > 0, "vertices outside the pad move");
    // The pad's source vertices stay current (unclipped, unmoved): C1 holds with them.
    checkClipped(doc, terrain, sourcesBefore, "pad", &pad->polygon);
    std::size_t onPad = 0;
    for (const auto *source : stateOf(doc, terrain)->sources())
        for (const auto &p : source->points)
            onPad += p.x >= c.x + 11 && p.x <= c.x + 17 && p.y >= c.y - 3 && p.y <= c.y + 3;
    check(onPad > 0, "source vertices on the pad stay in the current sources");
    checkC1(doc, terrain, "pad");
    checkC6(doc, terrain, "pad");
    checkC7(doc, terrain, "pad");
    // A brush wholly on the pad moves nothing and is refused.
    checkUnchanged(doc, "a brush on the pad", [&] {
        direct(doc, sculptCommand(terrain, {{"mode", "smooth"},
                                            {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{14, 0}}))},
                                            {"radius", 1.5}}));
    }, "INVALID_REQUEST");
    // Removing the pad's entry lifts the protection: the replayed sculpt moves those vertices.
    run(doc, command("terrain.remove_edit", terrain, {{"edit", 1}}));
    const auto lifted = checkRaise(vertices, heights(doc, terrain), lastSculpt(doc, terrain), "pad removed");
    check(lifted > moved, "without the pad the sculpt moves the vertices it protected");
    checkC1(doc, terrain, "pad removed");
    checkClipped(doc, terrain, sourcesBefore, "pad removed");
    checkC7(doc, terrain, "pad removed");
    std::cout << "pads: " << report["verticesProtected"].toDouble() << " protected, " << moved
              << " moved; " << lifted << " after removing the pad\n";
}

// A split piece is a source like any other: edit it by its id; removing the sculpt then skips
// that edit (its source no longer exists), reported and kept; C1 leaves the skipped entry out.
void pieces() {
    Document doc;
    const auto terrain = importRings(doc);
    run(doc, sculptCommand(terrain, {{"mode", "raise"},
                                     {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{-20, 0}, {20, 0}}))},
                                     {"radius", 3},
                                     {"amount", 0.4}}));
    const auto id = *terrainSplitSourceId(1, 0);
    const auto piece = stateOf(doc, terrain)->find(id);
    check(piece != nullptr, "the sculpt split a ring");
    const double z = piece->points.front().z;
    run(doc, command("terrain.edit_source", terrain,
                     {{"source", double(id)}, {"elevation", toModel(doc, terrain, {0, 0, z + 0.2}).z}}));
    check(stateOf(doc, terrain)->find(id)->points.front().z == z + 0.2,
          "a piece is re-elevated by its id");
    checkC1(doc, terrain, "piece edited");
    checkC6(doc, terrain, "piece edited");
    checkC7(doc, terrain, "piece edited");
    auto reopened = decodeDocument(encodeDocument(doc));
    checkC7(reopened, terrain, "piece edited (reopened)");
    const auto removed = run(doc, command("terrain.remove_edit", terrain, {{"edit", 1}}));
    const auto edits = removed["terrainReport"].toObject()["terrain"].toObject()["edits"].toObject();
    check(edits["skipped"] == 1 && recordOf(doc, terrain).edits.size() == 1,
          "the piece's edit is skipped once the sculpt is gone, and kept");
    const auto c1 = checkTerrainSourceConsistency(doc, terrain, {2});
    check(c1.ok(), "C1 holds with the skipped entry left out");
    // Ids are never reused: a new sculpt splits into ids of its own entry.
    run(doc, sculptCommand(terrain, {{"mode", "raise"},
                                     {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{-20, 0}, {20, 0}}))},
                                     {"radius", 3},
                                     {"amount", 0.4}}));
    check(recordOf(doc, terrain).edits.back()->id == 3 &&
              stateOf(doc, terrain)->find(*terrainSplitSourceId(3, 0)) != nullptr,
          "a later sculpt's pieces take its own ids");
    // Recorded piece ids are stable: removing an earlier change that makes the sculpt split one
    // more ring does not renumber the pieces of the others, so a later edit of a piece still
    // finds its geometry.
    {
        Document other;
        const auto t = importRings(other);
        auto rings = contours(*stateOf(other, t));
        run(other, command("terrain.remove_source", t, {{"source", double(rings.at(105.0)->id)}}));
        const auto stroke = sculptCommand(t, {{"mode", "raise"},
                                              {"path", planPoints(other, t, relPlan(other, t, {{-30, 0}, {30, 0}}))},
                                              {"radius", 3},
                                              {"amount", 0.3}});
        run(other, stroke);
        const auto &recorded = lastSculpt(other, t).pieces;
        check(!recorded.empty(), "the sculpt records its pieces");
        const auto parent = rings.at(103.5)->id;
        std::uint32_t pieceId = 0;
        for (const auto &[from, piece] : recorded)
            if (from == parent)
                pieceId = piece;
        check(pieceId != 0, "ring 3 was split");
        const double level = stateOf(other, t)->find(pieceId)->points.front().z;
        run(other, command("terrain.edit_source", t,
                           {{"source", double(pieceId)},
                            {"elevation", toModel(other, t, {0, 0, level + 0.1}).z}}));
        const auto replayed = run(other, command("terrain.remove_edit", t, {{"edit", 1}}));
        check(replayed["terrainReport"].toObject()["terrain"].toObject()["edits"].toObject()["skipped"] == 0,
              "with ring 0 back, the edit of ring 3's piece still applies");
        const auto *edited = stateOf(other, t)->find(pieceId);
        check(edited && edited->points.front().z == level + 0.1,
              "ring 3's piece keeps its id and its edit");
        checkC1(other, t, "stable pieces");
        checkC7(other, t, "stable pieces");
    }
    std::cout << "pieces: edited by id, skipped after Remove change, ids not reused, ids stable\n";
}

// The format: the codec round trip and its refusals.
void codec() {
    TerrainSculptData brush;
    brush.path = {{1.5, -2}, {3, 4}};
    brush.radius = 2.5;
    brush.falloff = TerrainFalloff::Constant;
    brush.amount = -0.25;
    auto data = std::make_shared<TerrainSculptData>(brush);
    const auto json = encodeTerrainEdit({7, terrainSculptKind, data});
    check(json["path"].isArray() && json["radius"] == 2.5 && json["falloff"] == "constant" &&
              json["amount"] == -0.25 && !json.contains("strength"),
          "a brush sculpt encodes its fields");
    const auto back = decodeTerrainEdit(json);
    check(back && *back == TerrainEdit{7, terrainSculptKind, data}, "the codec round-trips");
    TerrainSculptData smooth;
    smooth.region = {{0, 0}, {4, 0}, {0, 3}};
    smooth.width = 1;
    smooth.strength = 0.5;
    smooth.iterations = 8;
    auto smoothData = std::make_shared<TerrainSculptData>(smooth);
    const auto smoothJson = encodeTerrainEdit({8, terrainSmoothKind, smoothData});
    check(*decodeTerrainEdit(smoothJson) == TerrainEdit{8, terrainSmoothKind, smoothData},
          "a region smooth round-trips");
    auto refuses = [](QJsonObject entry, const std::string &what) {
        bool threw = false;
        try {
            (void)decodeTerrainEdit(entry);
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw, "the decoder refuses " + what);
    };
    auto bad = smoothJson;
    bad["iterations"] = 9;
    refuses(bad, "9 iterations");
    bad = smoothJson;
    bad["region"] = QJsonArray{QJsonArray{0, 0}, QJsonArray{1, 1}};
    refuses(bad, "a two-point region");
    bad = smoothJson;
    bad["amount"] = 1;
    refuses(bad, "a smooth with an amount");
    bad = json;
    bad["amount"] = 0;
    refuses(bad, "an amount of 0");
    bad = json;
    bad.remove("radius");
    refuses(bad, "a brush without a radius");
    bad = json;
    bad["falloff"] = "cubic";
    refuses(bad, "an unknown falloff");
    bad = json;
    bad["path"] = QJsonArray{QJsonArray{1, 2, 3}};
    refuses(bad, "a path point with Z");
    // Recorded pieces round-trip; a piece id below the split range is refused.
    brush.pieces = {{12, 4294967295u}, {12, 4294967294u}, {40, 4294967293u}};
    auto withPieces = std::make_shared<TerrainSculptData>(brush);
    const auto piecesJson = encodeTerrainEdit({1, terrainSculptKind, withPieces});
    check(piecesJson["pieces"].toArray().size() == 3 &&
              *decodeTerrainEdit(piecesJson) == TerrainEdit{1, terrainSculptKind, withPieces},
          "recorded pieces round-trip");
    bad = piecesJson;
    bad["pieces"] = QJsonArray{QJsonArray{12, 7}};
    refuses(bad, "a piece id below the split range");
    bad["pieces"] = QJsonArray{};
    refuses(bad, "an empty pieces list");
    std::cout << "codec: round trips and refusals\n";
}

// Assistant visibility (decision 9): terrain.sculpt is MCP-only; the in-app assistant's tools
// do not include it (assistant_tool_surface_tests pins the whole surface).
void assistantSurface() {
    Document doc;
    const auto terrain = importRings(doc, 6, 24);
    const auto documentId = QString::fromStdString(doc.identity());
    const auto revision = QString::number(doc.revision());
    run(doc, sculptCommand(terrain, {{"mode", "raise"},
                                     {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{13, 0}}))},
                                     {"radius", 4},
                                     {"amount", 1}}));
    const QJsonObject query{{"apiVersion", 1},
                            {"documentId", documentId},
                            {"expectedRevision", QString::number(doc.revision())},
                            {"query", "terrain.describe"},
                            {"body", QString::number(terrain)}};
    AssistantTask::Options options;
    options.prompt = "Describe the terrain.";
    options.provider = "fixture";
    options.model = "fixture";
    options.context = QJsonArray{query};
    AssistantTask task(AssistantBackend{[&](const QJsonObject &request) {
                                            return inspectDocument(doc, request);
                                        },
                                        [&] {
                                            return QJsonObject{{"documentId", documentId},
                                                               {"revision", QString::number(doc.revision())}};
                                        }},
                       options);
    const auto pending = task.nextRequest();
    check(bool(pending), "the assistant asks the provider");
    const auto request = QJsonDocument(*pending).toJson(QJsonDocument::Compact);
    check(!request.contains("terrain.sculpt") && request.contains("\"sculpt\""),
          "the assistant has no terrain.sculpt tool and reads the sculpt in terrain.describe");
    (void)revision;
    std::cout << "assistant: terrain.sculpt hidden, the edit listed\n";
}


// Review fixes (R096.b). A comb whose outline crosses one horizontal band 20,000 times is refused
// quickly with LIMIT_EXCEEDED and its band located (the command, the decoder and the validator);
// a comb at the bound (512 edges a band) is accepted and its inside test reads one band.
std::vector<std::array<double, 2>> comb(double x0, double y0, double width, double height,
                                        int teeth) {
    // Teeth upward from a base strip: 4 corners a tooth.
    std::vector<std::array<double, 2>> points;
    const double step = width / teeth;
    for (int i = 0; i < teeth; ++i) {
        const double x = x0 + step * i;
        points.push_back({x, y0});
        points.push_back({x, y0 + height});
        points.push_back({x + step / 2, y0 + height});
        points.push_back({x + step / 2, y0 + height * 0.01});
    }
    points.push_back({x0 + width, y0 + height * 0.01});
    points.push_back({x0 + width, y0 - height * 0.05});
    points.push_back({x0, y0 - height * 0.05});
    return points;
}
void hostileRegion() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    const auto hostile = comb(c.x - 150, c.y - 100, 300, 200, 4999);
    check(hostile.size() == 19999 && terrainSculptRegionBands(hostile).edges > 9000,
          "the comb crosses each band thousands of times");
    QJsonObject fields{{"mode", "smooth"},
                       {"region", planPoints(doc, terrain, hostile)},
                       {"falloffWidth", 5}};
    // One run: refused with LIMIT_EXCEEDED, changing nothing; the refusal itself is timed.
    Refusal refused;
    double seconds = 0;
    checkUnchanged(doc, "a comb region", [&] {
        const auto started = std::chrono::steady_clock::now();
        refused = refusal([&] { direct(doc, sculptCommand(terrain, fields)); });
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (!refused.code.empty())
            throw TerrainError(refused.code, refused.message);
    }, "LIMIT_EXCEEDED");
    check(!refused.report.isEmpty() &&
              refused.message.find("horizontal band") != std::string::npos,
          "a comb region is refused with LIMIT_EXCEEDED and a located report");
    check(seconds < 5, "the comb is refused quickly");
    TerrainSculptData data;
    data.region = hostile;
    data.width = 5;
    data.strength = 0.5;
    data.iterations = 1;
    check(refusal([&] { validateTerrainSculptData(terrainSmoothKind, data); }).code == "other",
          "the validator refuses the comb");
    auto encoded = encodeTerrainEdit({1, terrainSmoothKind, std::make_shared<TerrainSculptData>(data)});
    check(refusal([&] { (void)decodeTerrainEdit(encoded); }).code == "other",
          "the decoder refuses the comb");
    // At the bound: 128 teeth (513 corners) give at most 512 edges a band; accepted, and the
    // inside test agrees with brute force.
    auto bounded = comb(c.x - 150, c.y - 100, 300, 200, 128);
    const auto bands = terrainSculptRegionBands(bounded);
    check(bands.edges <= terrainSculptBandEdges && bands.edges >= 256,
          "a 128-tooth comb is at the band bound");
    data.region = bounded;
    validateTerrainSculptData(terrainSmoothKind, data);
    const TerrainSculptInfluence influence(data);
    std::mt19937_64 random(7);
    std::uniform_real_distribution<double> x(c.x - 160, c.x + 160), y(c.y - 120, c.y + 120);
    for (int i = 0; i < 2000; ++i) {
        const double px = x(random), py = y(random);
        int winding = 0;
        for (std::size_t k = 0; k < bounded.size(); ++k) {
            const auto &a = bounded[k], &b = bounded[(k + 1) % bounded.size()];
            const int o = terrainOrientation({a[0], a[1]}, {b[0], b[1]}, {px, py});
            if (a[1] <= py) {
                if (b[1] > py && o > 0)
                    ++winding;
            } else if (b[1] <= py && o < 0)
                --winding;
        }
        check(influence.inside(px, py) == (winding != 0), "the bounded comb's inside test");
    }
    const auto applied = run(doc, sculptCommand(terrain, {{"mode", "smooth"},
                                                          {"region", planPoints(doc, terrain, bounded)},
                                                          {"falloffWidth", 5}}));
    check(applied["verticesMoved"].toDouble() > 100, "the bounded comb smooths");
    checkC6(doc, terrain, "comb");
    checkC7(doc, terrain, "comb");
    std::cout << "hostile region: 19,999-corner comb refused in " << seconds << " s; a "
              << bands.edges << "-edge band comb accepted\n";
}

// A long entry can be cancelled part-way: the sculpt plan checks the replay's progress.
void cancelInsideEntry() {
    Document doc;
    const auto terrain = importPoints(doc);
    const auto c = centre(doc, terrain);
    auto data = std::make_shared<TerrainSculptData>();
    data->path = {{c.x, c.y}};
    data->radius = 400;
    data->strength = 0.5;
    data->iterations = 2;
    const TerrainEdit entry{1, terrainSmoothKind, data};
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    const auto state = stateOf(doc, terrain);
    TerrainProgress progress([] { return true; });
    bool cancelled = false;
    try {
        (void)planTerrainSculpt({*mesh, *state, recordOf(doc, terrain).parameters, &progress}, entry);
    } catch (const TerrainCancelled &) {
        cancelled = true;
    }
    check(cancelled, "a cancelled replay stops inside a sculpt entry");
    const auto plan = planTerrainSculpt({*mesh, *state, recordOf(doc, terrain).parameters}, entry);
    check(plan.change.lifted.size() > 1000, "without cancellation the entry plans");
    std::cout << "cancel: inside a " << plan.change.lifted.size() << "-vertex smooth\n";
}

// The area check is exact: a bow-tie (zero signed area, not on one line) is a region; corners on
// one line are not, whatever the platform's long double.
void exactArea() {
    TerrainSculptData bow;
    bow.region = {{0, 0}, {2, 2}, {2, 0}, {0, 2}};
    bow.amount = 1;
    validateTerrainSculptData(terrainSculptKind, bow);
    check(TerrainSculptInfluence(bow).inside(0.5, 1) && TerrainSculptInfluence(bow).inside(1.5, 1),
          "a bow-tie region holds both lobes");
    TerrainSculptData line = bow;
    line.region = {{0, 0}, {1e5, 1}, {3e5, 3}, {2e5, 2}};
    check(refusal([&] { validateTerrainSculptData(terrainSculptKind, line); }).code == "other",
          "corners on one line are refused");
    line.region[3] = {2e5, std::nextafter(2.0, 3.0)};
    validateTerrainSculptData(terrainSculptKind, line);
    std::cout << "exact area: bow-tie accepted, collinear refused, one ulp off the line accepted\n";
}

// Recorded pieces must come from their own entry's block: a document whose sculpt names another
// entry's id is refused on load, and the hook refuses such data rather than using it.
void pieceBlock() {
    Document doc;
    const auto terrain = importRings(doc);
    run(doc, sculptCommand(terrain, {{"mode", "raise"},
                                     {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{-20, 0}, {20, 0}}))},
                                     {"radius", 3},
                                     {"amount", 0.4}}));
    const auto &pieces = lastSculpt(doc, terrain).pieces;
    check(!pieces.empty() && pieces.front().second == *terrainSplitSourceId(1, 0),
          "the sculpt records its first piece");
    auto bytes = encodeDocument(doc);
    const auto own = QByteArray::number(qulonglong(*terrainSplitSourceId(1, 0)));
    const auto other = QByteArray::number(qulonglong(*terrainSplitSourceId(2, 0)));
    check(bytes.count(own) == 1, "the piece id appears once in the document");
    bytes.replace(own, other);
    const auto load = refusal([&] { (void)decodeDocument(bytes); });
    check(load.code != "" && load.message.find("own block") != std::string::npos,
          "a piece id from another entry's block is refused on load (" + load.message + ")");
    auto crafted = std::make_shared<TerrainSculptData>(lastSculpt(doc, terrain));
    crafted->pieces.front().second = *terrainSplitSourceId(2, 0);
    doc.undo();
    const auto clip = planTerrainSculptClip(*stateOf(doc, terrain), {1, terrainSculptKind, crafted});
    check(!clip.refusal.empty(), "the hook refuses a piece from another block");
    std::cout << "piece block: refused on load and in the hook\n";
}

constexpr const char *sequenceDigest =
    "81f5fd22dd04f88758cab9f151e265520e4323d407fdf1f12f8a10b05283b688";
// A scripted sequence: raise, region smooth, a contour edit of an untouched ring, a lower, a
// brush smooth; the checks after each; Undo all; Redo all; reopen; the digest pinned.
void sequence() {
    Document doc;
    const auto terrain = importRings(doc);
    {
        auto display = recordOf(doc, terrain).display;
        display.contourInterval = 0.5;
        setTerrainDisplay(doc, terrain, display);
    }
    TerrainContourCache cache;
    (void)cache.get(doc, terrain);
    const auto start = model(doc);
    std::vector<QJsonObject> steps{
        sculptCommand(terrain, {{"mode", "raise"},
                                {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{-40, 30}, {-10, 36}, {30, 40}}))},
                                {"radius", 6},
                                {"amount", 2}}),
        sculptCommand(terrain, {{"mode", "smooth"},
                                {"region", planPoints(doc, terrain, relPlan(doc, terrain, {{-50, -50}, {-20, -50}, {-20, -20}, {-50, -20}}))},
                                {"falloffWidth", 4},
                                {"strength", 1},
                                {"iterations", 4}}),
        sculptCommand(terrain, {{"mode", "raise"},
                                {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{45, -10}}))},
                                {"radius", 8},
                                {"falloff", "linear"},
                                {"amount", -1.25}}),
        sculptCommand(terrain, {{"mode", "smooth"},
                                {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{-45, 32}, {-20, 37}}))},
                                {"radius", 5},
                                {"strength", 0.6},
                                {"iterations", 8}})};
    std::vector<QByteArray> states;
    int index = 0;
    for (const auto &step : steps) {
        const auto snap = snapshot(doc, terrain);
        run(doc, step);
        const auto when = "sequence step " + std::to_string(++index);
        checkC5(doc, terrain, snap, when);
        checkAll(doc, terrain, cache, when);
        states.push_back(encoded(doc));
    }
    // A contour edit after the sculpts, on a ring they did not reach.
    {
        const auto state = stateOf(doc, terrain);
        const TerrainSource *ring = nullptr;
        for (const auto *source : state->sources())
            if (!ring && source->role == TerrainSourceRole::Contour && source->points.size() > 100 &&
                source->points.front() == source->points.back())
                ring = source;
        check(ring != nullptr, "a ring is untouched by the sculpts");
        run(doc, command("terrain.edit_source", terrain,
                         {{"source", double(ring->id)},
                          {"elevation", toModel(doc, terrain, {0, 0, ring->points.front().z + 0.25}).z}}));
        checkAll(doc, terrain, cache, "sequence contour edit");
    }
    const auto digest = recordOf(doc, terrain).meshDigest;
    std::cout << "sequence digest " << digest << "\n";
    if (std::string(sequenceDigest).size())
        check(digest == sequenceDigest, "the sequence's replay digest is pinned");
    const auto end = encoded(doc);
    for (std::size_t i = 0; i < steps.size() + 1; ++i)
        doc.undo();
    check(model(doc) == start, "undoing the sequence restores the start");
    for (std::size_t i = 0; i < steps.size() + 1; ++i)
        doc.redo();
    check(encoded(doc) == end, "redoing the sequence restores the end");
    auto reopened = decodeDocument(encodeDocument(doc));
    checkC1(reopened, terrain, "sequence (reopened)");
    checkC6(reopened, terrain, "sequence (reopened)");
    checkC7(reopened, terrain, "sequence (reopened)");
    // Reset edits returns the base.
    run(doc, command("terrain.reset_edits", terrain));
    check(model(doc).size() > 0 && recordOf(doc, terrain).edits.empty(), "Reset edits empties the list");
    checkC1(doc, terrain, "sequence reset");
    std::cout << "sequence: " << steps.size() << " sculpts and a contour edit, undone, redone, "
              << "reopened\n";
}
} // namespace

// R096.f1 (ADR 0169 §5, the terrain preview path): the desktop preview of a sculpt plans the
// change on the cached edit mesh and changes nothing; its candidate display (normals of the
// candidate tiles and their seam ring, outer edges, contours per candidate tile) equals what the
// per-tile caches compute after the commit, bit for bit; terrain.sculpt commits the previewed plan
// (the remembered plan) and an independent plan gives the same tiles; and a typed value after the
// commit revises the same History entry through the terrain amend policy, which may replace a
// different set of the terrain's tiles.
void desktopPreview() {
    Document doc;
    const auto terrain = importRings(doc);
    {
        auto display = recordOf(doc, terrain).display;
        display.contours = true;
        display.contourInterval = 0.5;
        setTerrainDisplay(doc, terrain, display);
    }
    TerrainSurfaceCache surfaces;
    TerrainContourCache contourCache;
    const auto surfacesBefore = surfaces.get(doc, terrain);
    (void)contourCache.get(doc, terrain);
    const auto path = relPlan(doc, terrain, {{-12, 2}, {-3, 1}, {9, -2}});
    const auto points = planPoints(doc, terrain, path);
    const auto sculpt = sculptCommand(terrain, {{"mode", "raise"},
                                                {"path", points},
                                                {"radius", 4.5},
                                                {"falloff", "smooth"},
                                                {"amount", 1.25}});
    // The data as the desktop builds it from the command (terrain.sculpt's conversion).
    const auto toLocal = doc.worldTransform(terrain).inverse();
    auto data = std::make_shared<TerrainSculptData>();
    for (const auto &value : points) {
        const auto xy = value.toArray();
        const auto local = toLocal.point({xy[0].toDouble(), xy[1].toDouble(), 0});
        data->path.push_back({local.x + 0.0, local.y + 0.0});
    }
    data->radius = 4.5;
    data->amount = 1.25;
    const auto before = encoded(doc);
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    const bool dirty = doc.dirty();
    const auto preview = previewTerrainSculpt(doc, terrain, terrainSculptKind, data);
    check(preview.edit && !preview.moving.empty() && preview.plan.refusal.empty(),
          "the preview plans a change");
    check(preview.moving.size() == preview.plan.change.lifted.size() &&
              std::all_of(preview.moving.begin(), preview.moving.end(),
                          [](const auto &m) { return m.second > 0 && m.second <= 1; }),
          "every moving vertex has its weight");
    check(encoded(doc) == before && doc.revision() == revision && doc.history().total == history &&
              doc.dirty() == dirty,
          "a preview changes no revision, history, dirty state or encoding");
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    const auto display = terrainPreviewDisplay(*mesh, preview.edit->region);
    std::set<TerrainTileKey> candidateKeys;
    for (const auto &tile : preview.edit->region.tiles)
        candidateKeys.insert(tile->key);
    std::size_t ring = 0;
    for (const auto &tile : display.tiles)
        ring += !tile.candidate;
    check(ring > 0 && display.hidden.size() == preview.edit->region.replaced.size() + ring,
          "the preview hides the replaced tiles and their seam ring");
    const auto offset = terrainElevationOffset(doc, terrain);
    std::map<TerrainTileKey, std::vector<TerrainContourSegment>> previewContours;
    for (const auto &tile : preview.edit->region.tiles)
        previewContours[tile->key] = terrainTileContours(*tile, 0.5, offset);
    // Commit: the remembered plan is taken.
    run(doc, sculpt);
    check(!takeRememberedTerrainSculptPlan(doc, terrain, terrainSculptKind, *data),
          "the commit took the remembered plan");
    std::map<TerrainTileKey, BodyPtr> committed;
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        committed[tile->tileKey] = tile;
    for (const auto &tile : preview.edit->region.tiles)
        check(committed.contains(tile->key) &&
                  terrainTileContent(*committed.at(tile->key))->sameMesh(*tile),
              "the committed tiles equal the previewed candidate bit for bit");
    // The preview's normals and outer edges are the committed per-tile surfaces, bit for bit.
    const auto after = surfaces.get(doc, terrain);
    std::set<TerrainTileKey> drawn;
    for (const auto &tile : display.tiles) {
        drawn.insert(tile.content->key);
        const auto &body = committed.at(tile.content->key);
        const auto surface = after->tile(body->id);
        check(surface && surface->normals == tile.normals,
              "a drawn tile's normals equal the committed surface's bit for bit");
        check(surface->outerEdges.size() == tile.outerEdges.size(),
              "a drawn tile's outer edges are the committed ones");
    }
    for (const auto &[id, tile] : tilesOf(doc, terrain))
        if (!drawn.contains(tile->tileKey))
            check(after->tile(id) == surfacesBefore->tile(id),
                  "every tile the preview does not draw keeps its surface");
    // Contours: each candidate tile's segments are the committed tile's.
    const auto contours = contourCache.get(doc, terrain);
    for (std::size_t i = 0; i < contours->tileStarts.size(); ++i) {
        const auto [id, first] = contours->tileStarts[i];
        const auto last = i + 1 < contours->tileStarts.size() ? contours->tileStarts[i + 1].second
                                                              : contours->segments.size();
        const auto key = doc.bodies().at(id)->tileKey;
        if (!previewContours.contains(key))
            continue;
        const auto &expected = previewContours.at(key);
        check(last - first == expected.size(), "a candidate tile's contour count is the commit's");
        for (std::size_t s = 0; s < expected.size(); ++s)
            check(contours->segments[first + s].a == expected[s].a &&
                      contours->segments[first + s].b == expected[s].b &&
                      contours->segments[first + s].index == expected[s].index,
                  "a candidate tile's contours are the commit's bit for bit");
    }
    // An independent plan (no remembered plan) gives the same tiles.
    {
        Document other;
        const auto copy = importRings(other);
        forgetRememberedTerrainSculptPlan();
        run(other, sculptCommand(copy, {{"mode", "raise"},
                                        {"path", planPoints(other, copy, path)},
                                        {"radius", 4.5},
                                        {"falloff", "smooth"},
                                        {"amount", 1.25}}));
        std::map<TerrainTileKey, BodyPtr> tiles;
        for (const auto &[id, tile] : tilesOf(other, copy))
            tiles[tile->tileKey] = tile;
        for (const auto &tile : preview.edit->region.tiles)
            check(tiles.contains(tile->key) &&
                      terrainTileContent(*tiles.at(tile->key))->sameMesh(*tile),
                  "an independent plan commits the previewed candidate");
    }
    // Amend: a typed amount, then a wider brush (other tiles), revise the same entry.
    const auto entries = doc.history().total;
    auto stamp = doc.amendmentStamp();
    auto amended = sculpt;
    amended["amount"] = -0.75;
    executeAmend(doc, stamp, batch(doc, {amended}));
    check(doc.history().total == entries && recordOf(doc, terrain).edits.size() == 1 &&
              lastSculpt(doc, terrain).amount == -0.75,
          "a typed amount revises the same entry");
    check(doc.history().entries.back().label == "Sculpt terrain",
          "the revised entry keeps its label");
    stamp = doc.amendmentStamp();
    amended["radius"] = 14.0;
    {
        // The generic policy refuses a replacement that replaces other tiles.
        auto copy = doc;
        auto copyStamp = copy.amendmentStamp();
        const auto refused = refusal([&] {
            copy.amendLast(copyStamp, [&](Document &candidate) {
                executeBatch(candidate, batch(candidate, {amended}));
            });
        });
        check(!refused.message.empty(), "the generic amend refuses other tiles");
    }
    executeAmend(doc, stamp, batch(doc, {amended}));
    check(doc.history().total == entries && lastSculpt(doc, terrain).radius == 14.0,
          "the terrain amend may replace other tiles of the terrain");
    checkC6(doc, terrain, "amend");
    checkC7(doc, terrain, "amend");
    {
        Document other;
        const auto copy = importRings(other);
        auto direct = amended;
        direct["terrain"] = QString::number(copy);
        direct["path"] = planPoints(other, copy, path);
        run(other, direct);
        std::map<TerrainTileKey, std::shared_ptr<const TerrainTileContent>> a, b;
        for (const auto &[id, tile] : tilesOf(doc, terrain))
            a[tile->tileKey] = terrainTileContent(*tile);
        for (const auto &[id, tile] : tilesOf(other, copy))
            b[tile->tileKey] = terrainTileContent(*tile);
        check(a.size() == b.size() &&
                  std::equal(a.begin(), a.end(), b.begin(), b.end(),
                             [](const auto &x, const auto &y) {
                                 return x.first == y.first && x.second->sameMesh(*y.second);
                             }),
              "the revised terrain's tiles equal the direct change's");
    }
    // The terrain policy revises only an edit that appended one change of the same kind: not a
    // display change, and not into a change of another kind.
    {
        auto copy = doc;
        auto smooth = amended;
        smooth["mode"] = "smooth";
        smooth.remove("amount");
        const auto otherKind = refusal([&] {
            executeAmend(copy, copy.amendmentStamp(), batch(copy, {smooth}));
        });
        check(!otherKind.message.empty() && lastSculpt(copy, terrain).amount == amended["amount"].toDouble(),
              "a revision into another kind is refused: " + otherKind.message);
        auto display = recordOf(copy, terrain).display;
        display.contourInterval = 2;
        setTerrainDisplay(copy, terrain, display);
        const auto entriesBefore = recordOf(copy, terrain).edits.size();
        const auto ofDisplay = refusal([&] {
            executeAmend(copy, copy.amendmentStamp(), batch(copy, {amended}));
        });
        check(!ofDisplay.message.empty() && recordOf(copy, terrain).edits.size() == entriesBefore,
              "a display change is not revised into a sculpt: " + ofDisplay.message);
    }
    // An intervening edit ends the revision.
    stamp = doc.amendmentStamp();
    auto paint = recordOf(doc, terrain).display;
    paint.contourInterval = 1;
    setTerrainDisplay(doc, terrain, paint);
    check(!doc.canAmend(stamp), "an intervening edit ends the revision");
    doc.undo();
    doc.undo();
    check(recordOf(doc, terrain).edits.empty(), "one Undo removes the revised sculpt");
    std::cout << "desktop preview: " << preview.moving.size() << " vertices, "
              << preview.edit->region.replaced.size() << " tiles, " << ring
              << " seam-ring tiles redrawn\n";
}

// R096.f1b: a terrain edit's commit defers the record's mesh digest (TerrainDigest): the commit
// does not compute it, and every reader (an equality, describe, "changed since generated", save,
// Undo and Redo) sees the value the eager digest of the committed tiles gives, whether it reads
// first or a worker computes it first.
void deferredDigest() {
    Document doc;
    const auto terrain = importRings(doc);
    auto eager = [&](const Document &d) {
        std::vector<const Body *> tiles;
        for (const auto id : terrainTiles(d, terrain))
            tiles.push_back(d.bodies().at(id).get());
        return terrainMeshDigest(tiles); // the definition: key-order SHA-256 of the tiles
    };
    const auto original = std::string(recordOf(doc, terrain).meshDigest);
    check(original == eager(doc) && !recordOf(doc, terrain).meshDigest.pending(),
          "an imported terrain's digest is known");
    auto sculpt = [&](double amount) {
        return sculptCommand(terrain,
                             {{"mode", "raise"},
                              {"path", planPoints(doc, terrain, relPlan(doc, terrain, {{-20, 0}, {-6, 1}, {20, 0}}))},
                              {"radius", 4.0},
                              {"amount", amount}});
    };
    // The commit leaves it pending: nothing computed it.
    const auto before = TerrainDigest::computations();
    run(doc, sculpt(0.75));
    check(recordOf(doc, terrain).meshDigest.pending() && TerrainDigest::computations() == before,
          "a sculpt's commit defers the digest");
    // "Changed since generated" (the Terrain section's chip, after every commit) answers from the
    // digest's identity without computing it.
    check(!terrainChangedSinceGenerated(doc, terrain) &&
              recordOf(doc, terrain).meshDigest.pending() &&
              TerrainDigest::computations() == before,
          "changed-since-generated during pending computes nothing");
    // The history charge holds the deferred digest's identity table.
    check(recordOf(doc, terrain).meshDigest.retainedBytes() >=
                  terrainTiles(doc, terrain).size() * sizeof(void *) &&
              terrainRecordBytes(recordOf(doc, terrain)) ==
                  terrainRecordBytes([&] {
                      auto known = recordOf(doc, terrain);
                      known.meshDigest = TerrainDigest(std::string(64, '0'));
                      return known;
                  }()) + recordOf(doc, terrain).meshDigest.retainedBytes(),
          "the history charge counts the deferred digest's identity table");
    // Describe while pending: the stored and the current digest are the committed tiles'.
    {
        const auto description = terrainDescription(doc, terrain);
        check(description["meshDigest"].toString().toStdString() == eager(doc) &&
                  description["currentDigest"] == description["meshDigest"] &&
                  !description["changedSinceGenerated"].toBool() &&
                  !terrainChangedSinceGenerated(doc, terrain),
              "describe during pending reads the committed tiles' digest");
    }
    // Save while pending (a second edit, still pending): the file holds the final digest and
    // reopens to the same record.
    run(doc, sculpt(-0.4));
    check(recordOf(doc, terrain).meshDigest.pending(), "the second sculpt is pending too");
    const auto expected = eager(doc);
    const auto bytes = encodeDocument(doc);
    check(QJsonDocument::fromJson(bytes).toJson().contains(QByteArray::fromStdString(expected)),
          "a save during pending writes the final digest");
    const auto reopened = decodeDocument(bytes);
    check(std::string(recordOf(reopened, terrain).meshDigest) == expected &&
              !terrainChangedSinceGenerated(reopened, terrain) && model(reopened) == model(doc),
          "the saved document reopens with the same digest");
    // Undo and Redo while pending.
    run(doc, sculpt(0.3));
    const auto third = recordOf(doc, terrain).meshDigest; // pending, shared
    check(third.pending(), "the third sculpt is pending");
    doc.undo();
    check(std::string(recordOf(doc, terrain).meshDigest) == expected && expected == eager(doc),
          "Undo restores the previous record's digest");
    doc.redo();
    check(recordOf(doc, terrain).meshDigest.pending() == third.pending() &&
              std::string(recordOf(doc, terrain).meshDigest) == eager(doc),
          "Redo restores the deferred digest, which reads the committed tiles' value");
    // A worker and a reader race: one computes, both see the same value.
    run(doc, sculpt(0.2));
    const auto racing = recordOf(doc, terrain).meshDigest;
    std::string fromWorker;
    std::thread worker([&] { fromWorker = racing.value(); });
    const std::string fromReader = racing.value();
    worker.join();
    check(fromWorker == fromReader && fromReader == eager(doc), "a worker and a reader agree");
    // Regenerate after the edits is a no-op (C7), and the record equality holds.
    checkC7(doc, terrain, "deferred digest");
    std::cout << "deferred digest: " << TerrainDigest::computations() - before
              << " deferred digests computed by readers\n";
}

// R096.f1b: the deferred digest's own guarantees, on a synthetic computation.
void deferredDigestState() {
    // pending() and a known value never wait for a computation in flight.
    {
        std::promise<void> started, release;
        auto releasing = release.get_future().share();
        auto tile = std::make_shared<int>(1);
        const auto digest = TerrainDigest::deferred(
            [&started, releasing](const std::vector<const void *> &) {
                started.set_value();
                releasing.wait();
                return std::string(64, 'a');
            },
            {tile});
        std::string computed;
        std::thread reader([&] { computed = digest.value(); });
        started.get_future().wait();
        const auto t0 = std::chrono::steady_clock::now();
        const bool pending = digest.pending();
        const bool describes = digest.describes({tile.get()});
        const auto waited = std::chrono::steady_clock::now() - t0;
        release.set_value();
        reader.join();
        check(pending && describes && waited < std::chrono::seconds(1) &&
                  computed == std::string(64, 'a') && !digest.pending() &&
                  digest.value() == computed,
              "pending and describes answer during a computation without waiting for it");
    }
    // The identity is weak: the computation holds the tiles until it has run, then only the
    // records holding them keep them, and an expired tile no longer matches its old address.
    {
        auto tile = std::make_shared<int>(2);
        const void *address = tile.get();
        auto digest = TerrainDigest::deferred(
            [address](const std::vector<const void *> &tiles) {
                check(tiles.size() == 1 && tiles[0] == address, "the hash gets the held tiles");
                return std::string(64, 'b');
            },
            {tile});
        check(tile.use_count() == 2, "the digest holds the tile while pending");
        (void)digest.value();
        check(tile.use_count() == 1 && digest.describes({address}),
              "the computed digest holds the tile weakly and still describes it");
        tile.reset();
        check(!digest.describes({address}), "an expired tile is not described");
    }
    // The prefetch task skips a digest nothing holds, and never throws.
    {
        int runs = 0;
        auto digest = TerrainDigest::deferred(
            [&runs](const std::vector<const void *> &) {
                ++runs;
                return std::string(64, 'c');
            },
            {});
        auto task = digest.prefetchTask();
        digest = TerrainDigest();
        task();
        check(runs == 0, "a prefetch of a dropped digest computes nothing");
        const auto failing = TerrainDigest::deferred(
            [](const std::vector<const void *> &) -> std::string {
                throw std::runtime_error("hash failed");
            },
            {});
        failing.prefetchTask()(); // must not throw
        bool threw = false;
        try {
            (void)failing.value();
        } catch (const std::runtime_error &) {
            threw = true;
        }
        check(failing.pending() && threw, "a failed computation stays pending for the next reader");
    }
    std::cout << "deferred digest state: pending, identity and prefetch\n";
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;
    const std::vector<std::pair<const char *, void (*)()>> tests{
        {"weights", weights},           {"raise brush", raiseBrush},
        {"smooth region", smoothRegion}, {"falloff edges", falloffEdges},
        {"sampling", sampling},         {"refusals", refusals},
        {"pad protection", padProtection}, {"pieces", pieces},
        {"codec", codec},               {"assistant surface", assistantSurface},
        {"hostile region", hostileRegion}, {"cancel inside entry", cancelInsideEntry},
        {"exact area", exactArea},      {"piece block", pieceBlock},
        {"sequence", sequence},         {"desktop preview", desktopPreview},
        {"deferred digest", deferredDigest}, {"deferred digest state", deferredDigestState}};
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
