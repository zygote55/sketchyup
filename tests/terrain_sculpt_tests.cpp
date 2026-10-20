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
    for (int i = 0; i < 1500; ++i) {
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

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;
    const std::vector<std::pair<const char *, void (*)()>> tests{
        {"weights", weights},           {"raise brush", raiseBrush},
        {"smooth region", smoothRegion}, {"falloff edges", falloffEdges},
        {"sampling", sampling},         {"refusals", refusals},
        {"pad protection", padProtection}, {"pieces", pieces},
        {"codec", codec},               {"assistant surface", assistantSurface},
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
