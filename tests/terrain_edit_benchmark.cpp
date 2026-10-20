// R096.a (ADR 0169 §5, §9 "Budgets"): the one-tile edit budget on the two 250,000-triangle
// terrains of terrain_budget_benchmark (125,000 random points; 416 contour rings), measured
// first with a test-only operation (terrain_edit_test_kinds.hpp, "test.lift": raise one tile's
// unshared vertices, which rebuilds exactly that tile).
//
// For each terrain it reports:
// - the commit of a one-tile edit (applyTerrainEdit: edit mesh lookup, the kind, the local tile
//   replacement, the record with its new entry, the key-order digest and Document::apply with
//   validation), N edits (default 200) on tiles spread over the terrain, largest tiles first;
//   p50, p95 and max, with the phases of one commit;
// - the cold build of the edit mesh and its memory, and the cache update after an edit;
// - terrainElevationAt for 200 points, indexed, against R095's full scan;
// - the undo depth: the terrain history charge after the N edits, the largest single charge
//   and the edits that fit the 512 MiB budget at that rate (decision 10: at least 64);
// - the seam check of the whole terrain;
// - R096.a part 2: the display update after each commit (the per-tile surface and contour
//   caches the viewport uses, contour lines at 1 m), and commit plus display;
// - the replaying Regenerate with M entries (default 1,000; decision 10 proposes <= 3 s): the
//   request, the CDT of the base and the replay, and the apply; and the cancel latency of that
//   replay, cancelled from another thread at TRIALS (default 20) times spread over the run.
// - R096.c: contour edits (terrain.edit_source and terrain.add_source). The preview of a contour
//   edit (previewTerrainEdit: the cached edit mesh and current sources, the cavity, the adapter
//   and the candidate tiles) for runs of 1 to 290 moved contour vertices and, on the points
//   terrain, added spot points and breaklines of 50 to 2,700 m, giving cavities up to and past
//   2,048 triangles (decision 10: p95 <= 100 ms up to 2,048); the cold derivation of the current sources; and the commit of a one-vertex contour
//   edit through applyTerrainEdit and through the whole command (parse, dry run, commit, report),
//   with the display update (decision 10: one-tile commit p95 <= 100 ms).
// - R096.b: sculpt and smooth (terrain.sculpt). Short brush strokes inside the largest tiles: the
//   preview computation (previewTerrainEdit: the weights, the clipping of the current sources, the
//   candidate tiles; ADR 0169 §5 runs it on the UI thread), the commit through applyTerrainEdit
//   and through the whole command, with the display update (decision 10: one-tile commit p95 <=
//   100 ms; samples whose touched tiles hold more than 2,048 triangles are reported apart);
//   a brush touching nine tiles; three smoothing iterations; and a replaying Regenerate of
//   S sculpt entries (default 1,000).
// Uploading to the GPU and drawing the frame are not included (no viewport here). Prints one
// JSON document. Build it Release.
//
// - R096.d: pad stamps (terrain.stamp_pad, terrain.update_edit); see measureStamps.
// - R096.e: drapes (terrain.drape); see measureDrapes.
//
//   terrain_edit_benchmark [--edits N] [--replay M] [--cancel TRIALS] [--source-edits K]
//                          [--sculpts J] [--sculpt-replay S] [--stamps T] [--stamp-replay P]
//                          [--drapes D] [--drape-replay R]
#include "automation/terrain_commands.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_sources.hpp"
#include "terrain_edit_test_kinds.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <atomic>
#include <numbers>
#include <thread>
#include <unistd.h>
using namespace sketchy;
namespace kinds = terrain_test_kinds;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
double percentile(std::vector<double> samples, double p) {
    std::sort(samples.begin(), samples.end());
    return samples.at(std::min(samples.size() - 1,
                               std::size_t(std::ceil(double(samples.size()) * p)) - 1));
}
double ms(const QElapsedTimer &timer) { return double(timer.nsecsElapsed()) / 1e6; }
// The sources of terrain_budget_benchmark, generated identically.
QByteArray pointSource(int points) {
    QByteArray file;
    std::uint32_t state = 20261019;
    auto random = [&] {
        state = state * 1664525u + 1013904223u;
        return double(state >> 8) / double(1u << 24);
    };
    for (int i = 0; i < points; ++i) {
        const double x = random() * 1000, y = random() * 1000;
        const double z = 1850 + 12 * std::sin(x / 90) * std::cos(y / 70) + 0.002 * x;
        file += QByteArray::number(512000 + x, 'f', 3) + ' ' +
                QByteArray::number(5234000 + y, 'f', 3) + ' ' + QByteArray::number(z, 'f', 3) +
                '\n';
    }
    return file;
}
QByteArray contourSource(int rings, int vertices) {
    QByteArray out = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n6\n0\n"
                     "ENDSEC\n0\nSECTION\n2\nENTITIES\n";
    for (int k = 0; k < rings; ++k) {
        const double r = 10 + k * 0.9, z = 100 + 0.5 * k;
        out += "0\nLWPOLYLINE\n5\n" + QByteArray::number(0x100 + k, 16).toUpper() +
               "\n8\nCONTOURS\n90\n" + QByteArray::number(vertices) + "\n70\n1\n38\n" +
               QByteArray::number(z, 'f', 4) + '\n';
        for (int i = 0; i < vertices; ++i) {
            const double t = 2 * std::numbers::pi * i / vertices + 0.01 * k;
            out += "10\n" + QByteArray::number(r * std::cos(t) * 1.6, 'f', 4) + "\n20\n" +
                   QByteArray::number(r * std::sin(t), 'f', 4) + '\n';
        }
    }
    out += "0\nENDSEC\n0\nEOF\n";
    return out;
}
QJsonObject stats(const std::vector<double> &samples) {
    if (samples.empty())
        return {{"count", 0}};
    return {{"count", double(samples.size())},
            {"p50", percentile(samples, .5)},
            {"p95", percentile(samples, .95)},
            {"max", *std::max_element(samples.begin(), samples.end())}};
}

// R096.c: contour-edit preview and commit (ADR 0169 §5, decision 10).
QJsonObject measureSourceEdits(Document &doc, Id terrain, int samples) {
    QElapsedTimer timer;
    clearTerrainSourceStateCache();
    timer.start();
    const auto state = terrainSourceState(*doc.bodies().at(terrain)->terrain);
    const double coldState = ms(timer);
    // The rings (closed contours) and their centre; a run of vertices moves a third of the way
    // to the next ring.
    std::vector<const TerrainSource *> rings;
    for (const auto *source : state->sources())
        if (source->role == TerrainSourceRole::Contour && source->points.size() > 200)
            rings.push_back(source);
    const auto world = doc.worldTransform(terrain);
    auto modelPoints = [&](const std::vector<Vec3> &points) {
        QJsonArray list;
        for (const auto &p : points) {
            const auto m = world.point(p);
            list.append(QJsonArray{m.x, m.y, m.z});
        }
        return list;
    };
    auto moved = [&](const TerrainSource &ring, std::size_t start, std::size_t run) {
        double cx = 0, cy = 0;
        for (std::size_t i = 0; i + 1 < ring.points.size(); ++i) {
            cx += ring.points[i].x;
            cy += ring.points[i].y;
        }
        cx /= double(ring.points.size() - 1);
        cy /= double(ring.points.size() - 1);
        auto points = ring.points;
        for (std::size_t i = start; i < start + run; ++i) {
            const double dx = points[i].x - cx, dy = points[i].y - cy, r = std::hypot(dx, dy);
            const double grow = (r + 0.3) / r;
            points[i].x = cx + dx * grow;
            points[i].y = cy + dy * grow;
        }
        return points;
    };
    QJsonObject result{{"currentSourcesColdMs", coldState}};
    if (rings.empty()) {
        // The points terrain: added spot points (its one point source is beyond an entry's
        // 20,000 points, so it is not edited in place).
        std::vector<double> preview, commit;
        std::size_t cavity = 0;
        const auto grid = doc.bodies().at(terrain)->terrain->tiling;
        for (int i = 0; i < samples; ++i) {
            const Vec3 p{grid.originX + 37.13 + 9.71 * (i % 90), grid.originY + 41.37 + 10.3 * (i % 87),
                         1860};
            auto data = std::make_shared<TerrainSourceEditData>();
            data->source = terrainAddedSourceId(state->maxBaseId(),
                                                doc.bodies().at(terrain)->terrain->nextEditId);
            data->role = TerrainSourceRole::Points;
            data->points = {p};
            timer.restart();
            const auto candidate = previewTerrainEdit(doc, terrain, terrainAddSourceKind, data);
            preview.push_back(ms(timer));
            cavity = std::max(cavity, candidate.removedTriangles);
            if (i < samples / 4) {
                // Each spot point adds two triangles to a terrain at the document limit, so each
                // commit is undone before the next.
                timer.restart();
                applyTerrainEdit(doc, terrain, terrainAddSourceKind, data, "Add contour");
                commit.push_back(ms(timer));
                doc.undo();
            }
        }
        result["addSpotPoint"] = QJsonObject{{"previewMs", stats(preview)},
                                             {"commitMs", stats(commit)},
                                             {"largestCavity", double(cavity)}};
        // Breaklines of growing length (a vertex every 5 m), for cavities up to 2,048 triangles
        // and beyond.
        QJsonArray lines;
        for (const double length : {50.0, 450.0, 900.0, 1800.0, 2000.0, 2700.0}) {
            std::vector<double> times;
            std::size_t lo = SIZE_MAX, hi = 0;
            for (int i = 0; i < std::max(1, samples / 4); ++i) {
                auto data = std::make_shared<TerrainSourceEditData>();
                data->source = terrainAddedSourceId(state->maxBaseId(),
                                                    doc.bodies().at(terrain)->terrain->nextEditId);
                data->role = TerrainSourceRole::Breakline;
                // A serpentine of 450 m rows 15 m apart, so long lines stay inside the terrain.
                const double x0 = grid.originX + 20.37 + 11.3 * i, y0 = grid.originY + 30.71 + 7.9 * i;
                for (double d = 0; d <= length; d += 5) {
                    const int row = int(d / 450);
                    const double along = d - 450 * row, x = row % 2 ? 450 - along : along;
                    data->points.push_back({x0 + x, y0 + 15.0 * row + along * 1e-3,
                                            1855 + d * 0.001});
                }
                timer.restart();
                const auto candidate = previewTerrainEdit(doc, terrain, terrainAddSourceKind, data);
                times.push_back(ms(timer));
                lo = std::min(lo, candidate.removedTriangles);
                hi = std::max(hi, candidate.removedTriangles);
            }
            lines.append(QJsonObject{{"lengthM", length},
                                     {"cavityTriangles", QJsonArray{double(lo), double(hi)}},
                                     {"previewMs", stats(times)}});
        }
        result["addBreakline"] = lines;
        // The cavity bound (terrainSourceEditCavityTriangles): a serpentine breakline whose cavity
        // stays just under it, and the worst case it guards against, a 20,000-point zigzag whose
        // every segment spans the terrain, refused while its cavity is collected.
        {
            auto serpentine = std::make_shared<TerrainSourceEditData>();
            serpentine->source = terrainAddedSourceId(state->maxBaseId(),
                                                      doc.bodies().at(terrain)->terrain->nextEditId);
            serpentine->role = TerrainSourceRole::Breakline;
            const double rowLength = 900, rowGap = 14.3, length = 58000;
            for (double d = 0; d <= length; d += 5) {
                const int row = int(d / rowLength);
                const double along = d - rowLength * row, x = row % 2 ? rowLength - along : along;
                serpentine->points.push_back({grid.originX + 40.37 + x,
                                              grid.originY + 20.71 + rowGap * row + along * 1e-4,
                                              1855 + d * 1e-5});
            }
            QElapsedTimer bound;
            bound.start();
            const auto near = previewTerrainEdit(doc, terrain, terrainAddSourceKind, serpentine);
            const double nearMs = ms(bound);
            auto zigzag = std::make_shared<TerrainSourceEditData>(*serpentine);
            zigzag->points.clear();
            for (int i = 0; i < int(terrainSourceEditPoints); ++i)
                zigzag->points.push_back({grid.originX + (i % 2 ? 960.37 : 20.71),
                                          grid.originY + 15.13 + 0.0481 * i, 1855});
            bound.restart();
            std::string code;
            try {
                (void)previewTerrainEdit(doc, terrain, terrainAddSourceKind, zigzag);
            } catch (const TerrainError &error) {
                code = error.code();
            }
            const double zigzagMs = ms(bound);
            check(code == "LIMIT_EXCEEDED", "the zigzag breakline is refused by the cavity bound");
            result["cavityBound"] =
                QJsonObject{{"bound", double(terrainSourceEditCavityTriangles)},
                            {"nearBoundPoints", double(serpentine->points.size())},
                            {"nearBoundCavity", double(near.removedTriangles)},
                            {"nearBoundPreviewMs", nearMs},
                            {"zigzagPoints", double(zigzag->points.size())},
                            {"zigzagRefusalMs", zigzagMs}};
        }
        return result;
    }
    // Preview: runs of moved vertices on rings across the terrain.
    QJsonArray runs;
    std::vector<double> all;
    std::size_t over = 0, largest = 0;
    for (const std::size_t run : {std::size_t(1), std::size_t(4), std::size_t(16), std::size_t(64),
                                  std::size_t(150), std::size_t(290)}) {
        std::vector<double> times;
        std::size_t lo = SIZE_MAX, hi = 0;
        for (int i = 0; i < samples; ++i) {
            const auto &ring = *rings[(std::size_t(i) * 37 + 11) % rings.size()];
            const std::size_t start = 1 + (std::size_t(i) * 53) % (ring.points.size() - run - 3);
            auto data = std::make_shared<TerrainSourceEditData>();
            data->source = ring.id;
            data->role = ring.role;
            data->points = moved(ring, start, run);
            data->before = {-1e6, -1e6, 1e6, 1e6};
            timer.restart();
            const auto candidate = previewTerrainEdit(doc, terrain, terrainEditSourceKind, data);
            const double t = ms(timer);
            lo = std::min(lo, candidate.removedTriangles);
            hi = std::max(hi, candidate.removedTriangles);
            largest = std::max(largest, candidate.removedTriangles);
            if (candidate.removedTriangles > TerrainLimits::tileTriangles) {
                ++over;
                continue;
            }
            times.push_back(t);
            all.push_back(t);
        }
        QJsonObject entry{{"movedVertices", double(run)},
                          {"cavityTriangles", QJsonArray{double(lo), double(hi)}}};
        if (!times.empty())
            entry["previewMs"] = stats(times);
        runs.append(entry);
    }
    result["preview"] = QJsonObject{{"runs", runs},
                                    {"allMs", stats(all)},
                                    {"largestCavity", double(largest)},
                                    {"overBudgetCavities", double(over)}};
    // Commit: one moved vertex per edit, through the core and through the command, then the
    // display update.
    TerrainSurfaceCache surfaces;
    TerrainContourCache contours;
    (void)surfaces.get(doc, terrain);
    (void)contours.get(doc, terrain);
    std::vector<double> core, command, display, commandAndDisplay, replacedTriangles;
    int committed = 0;
    for (int i = 0; i < samples; ++i) {
        const auto current = terrainSourceState(*doc.bodies().at(terrain)->terrain);
        const auto *ring = current->find(rings[(std::size_t(i) * 29 + 5) % rings.size()]->id);
        const std::size_t start = 1 + (std::size_t(i) * 41) % (ring->points.size() - 4);
        const auto points = moved(*ring, start, 1);
        if (i % 2 == 0) {
            auto data = std::make_shared<TerrainSourceEditData>();
            data->source = ring->id;
            data->role = ring->role;
            data->points = points;
            data->before = {-1e6, -1e6, 1e6, 1e6};
            timer.restart();
            const auto applied =
                applyTerrainEdit(doc, terrain, terrainEditSourceKind, data, "Edit contour");
            core.push_back(ms(timer));
            std::size_t triangles = 0;
            for (const auto id : applied.tiles.added)
                triangles += doc.bodies().at(id)->surface.faces.size();
            replacedTriangles.push_back(double(triangles));
        } else {
            const QJsonObject edit{{"command", "terrain.edit_source"},
                                   {"terrain", QString::number(terrain)},
                                   {"source", double(ring->id)},
                                   {"points", modelPoints(points)}};
            QJsonObject operation;
            timer.restart();
            executeTerrainCommand(doc, edit, &operation);
            command.push_back(ms(timer));
            QElapsedTimer update;
            update.start();
            (void)surfaces.get(doc, terrain);
            (void)contours.get(doc, terrain);
            display.push_back(ms(update));
            commandAndDisplay.push_back(command.back() + display.back());
        }
        ++committed;
    }
    // C1 and C6 after the commits, at 250k triangles.
    const auto sources = checkTerrainSourceConsistency(doc, terrain);
    const auto seams = checkTerrainSeams(doc, terrain);
    check(sources.ok() && seams.ok(), "C1 and C6 hold after the contour edits");
    result["consistency"] = QJsonObject{{"sources", double(sources.sources)},
                                        {"vertices", double(sources.vertices)},
                                        {"segments", double(sources.segments)},
                                        {"seamsOk", seams.ok()}};
    for (int i = 0; i < committed; ++i)
        doc.undo();
    result["commit"] = QJsonObject{{"coreMs", stats(core)},
                                   {"commandMs", stats(command)},
                                   {"displayUpdateMs", stats(display)},
                                   {"commandAndDisplayMs", stats(commandAndDisplay)},
                                   {"rebuiltTileTriangles", stats(replacedTriangles)}};
    return result;
}

// R096.b: sculpt and smooth (ADR 0169 §5, decision 10).
QJsonObject measureSculpts(Document &doc, Id terrain, int samples, int replayEntries) {
    QElapsedTimer timer;
    const auto world = doc.worldTransform(terrain);
    const auto grid = doc.bodies().at(terrain)->terrain->tiling;
    // The tiles, largest first, with their plan boxes.
    std::vector<std::pair<std::size_t, TerrainTileKey>> bySize;
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &tile = *doc.bodies().at(id);
        bySize.push_back({tile.surface.faces.size(), tile.tileKey});
    }
    std::stable_sort(bySize.begin(), bySize.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    // A short stroke through the middle of a tile, its radius a quarter of the tile's smaller
    // side, so it stays inside the tile (moved vertices on a seam touch the neighbour too).
    auto stroke = [&](const TerrainTileKey &key, double scale) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto *tile = mesh->tile(key);
        const double cx = (tile->minX + tile->maxX) / 2, cy = (tile->minY + tile->maxY) / 2;
        const double side = std::min(tile->maxX - tile->minX, tile->maxY - tile->minY);
        auto data = std::make_shared<TerrainSculptData>();
        data->radius = std::clamp(side * scale, terrainSculptRadiusMin, terrainSculptRadiusMax);
        data->path = {{cx - data->radius / 2, cy}, {cx, cy + data->radius / 4},
                      {cx + data->radius / 2, cy}};
        data->amount = 0.01;
        return data;
    };
    auto touchedTriangles = [&](const TerrainEditPreview &preview) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        std::size_t triangles = 0;
        for (const auto &key : preview.region.replaced)
            triangles += mesh->tile(key)->content->triangles.size();
        return triangles;
    };
    TerrainSurfaceCache surfaces;
    TerrainContourCache contours;
    (void)surfaces.get(doc, terrain);
    (void)contours.get(doc, terrain);
    std::vector<double> preview, previewOver, smoothPreview, core, command, display,
        commandAndDisplay, tilesTouched, triangles, moved;
    std::size_t over = 0;
    const auto pool = std::min<std::size_t>(bySize.size(), 64);
    for (int i = 0; i < samples; ++i) {
        const auto &key = bySize[std::size_t(i) % pool].second;
        auto data = stroke(key, 0.25);
        timer.restart();
        const auto candidate = previewTerrainEdit(doc, terrain, terrainSculptKind, data);
        const double previewMs = ms(timer);
        const auto touched = touchedTriangles(candidate);
        tilesTouched.push_back(double(candidate.region.replaced.size()));
        triangles.push_back(double(touched));
        moved.push_back(double(candidate.liftedVertices));
        if (touched > TerrainLimits::tileTriangles) {
            ++over;
            previewOver.push_back(previewMs);
            continue;
        }
        preview.push_back(previewMs);
        {
            auto smooth = std::make_shared<TerrainSculptData>(*data);
            smooth->amount = 0;
            smooth->strength = 0.5;
            smooth->iterations = 3;
            timer.restart();
            try {
                (void)previewTerrainEdit(doc, terrain, terrainSmoothKind, smooth);
                smoothPreview.push_back(ms(timer));
            } catch (const TerrainError &) {
                // Smoothing a patch that is already smooth moves nothing and is refused.
            }
        }
        if (i % 2 == 0) {
            timer.restart();
            applyTerrainEdit(doc, terrain, terrainSculptKind, data, "Sculpt terrain");
            core.push_back(ms(timer));
        } else {
            QJsonArray path;
            for (const auto &p : data->path) {
                const auto m = world.point({p[0], p[1], 0});
                path.append(QJsonArray{m.x, m.y});
            }
            const QJsonObject sculpt{{"command", "terrain.sculpt"},
                                     {"terrain", QString::number(terrain)},
                                     {"mode", "raise"},
                                     {"path", path},
                                     {"radius", data->radius},
                                     {"amount", 0.01}};
            QJsonObject operation;
            timer.restart();
            executeTerrainCommand(doc, sculpt, &operation);
            command.push_back(ms(timer));
        }
        QElapsedTimer update;
        update.start();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
        display.push_back(ms(update));
        if (i % 2)
            commandAndDisplay.push_back(command.back() + display.back());
    }
    const auto sources = checkTerrainSourceConsistency(doc, terrain);
    const auto seams = checkTerrainSeams(doc, terrain);
    check(sources.ok() && seams.ok(), "C1 and C6 hold after the sculpts");
    QJsonObject result{
        {"oneTile",
         QJsonObject{{"samples", samples},
                     {"overBudgetTiles", double(over)},
                     {"tilesTouched", stats(tilesTouched)},
                     {"touchedTriangles", stats(triangles)},
                     {"verticesMoved", stats(moved)},
                     {"previewMs", stats(preview)},
                     {"smoothPreview3IterationsMs", stats(smoothPreview)},
                     {"coreCommitMs", stats(core)},
                     {"commandCommitMs", stats(command)},
                     {"displayUpdateMs", stats(display)},
                     {"commandAndDisplayMs", stats(commandAndDisplay)}}},
        {"consistency", QJsonObject{{"sources", double(sources.sources)},
                                    {"vertices", double(sources.vertices)},
                                    {"seamsOk", seams.ok()}}}};
    if (!previewOver.empty())
        result["oneTileOverBudgetPreviewMs"] = stats(previewOver);
    // A brush touching nine tiles: centred on a cell of the grid, its radius a cell.
    {
        std::vector<double> nine, nineCommit, nineDisplay, nineTiles, nineMoved, nineTriangles;
        for (int i = 0; i < std::max(1, samples / 5); ++i) {
            const auto &key = bySize[std::size_t(i * 7) % pool].second;
            const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
            const auto *tile = mesh->tile(key);
            auto data = std::make_shared<TerrainSculptData>();
            const double cx = (tile->minX + tile->maxX) / 2, cy = (tile->minY + tile->maxY) / 2;
            data->radius = std::clamp(grid.cell * 1.1, terrainSculptRadiusMin, terrainSculptRadiusMax);
            data->path = {{cx, cy}};
            data->amount = 0.01;
            timer.restart();
            const auto candidate = previewTerrainEdit(doc, terrain, terrainSculptKind, data);
            nine.push_back(ms(timer));
            nineTiles.push_back(double(candidate.region.replaced.size()));
            nineMoved.push_back(double(candidate.liftedVertices));
            nineTriangles.push_back(double(touchedTriangles(candidate)));
            timer.restart();
            applyTerrainEdit(doc, terrain, terrainSculptKind, data, "Sculpt terrain");
            nineCommit.push_back(ms(timer));
            QElapsedTimer update;
            update.start();
            (void)surfaces.get(doc, terrain);
            (void)contours.get(doc, terrain);
            nineDisplay.push_back(ms(update));
        }
        result["brushOverCells"] = QJsonObject{{"radiusCells", 1.1},
                                               {"tilesTouched", stats(nineTiles)},
                                               {"touchedTriangles", stats(nineTriangles)},
                                               {"verticesMoved", stats(nineMoved)},
                                               {"previewMs", stats(nine)},
                                               {"coreCommitMs", stats(nineCommit)},
                                               {"displayUpdateMs", stats(nineDisplay)}};
    }
    // Hostile regions (review of R096.b): a comb whose outline crosses each horizontal band
    // thousands of times is refused before any work; a comb at the band bound (at most 512 edges
    // a band) over most of the terrain previews in bounded time.
    {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        for (const auto *tile : mesh->tiles()) {
            minX = std::min(minX, tile->minX);
            minY = std::min(minY, tile->minY);
            maxX = std::max(maxX, tile->maxX);
            maxY = std::max(maxY, tile->maxY);
        }
        auto comb = [&](int teeth) {
            const double x0 = minX + (maxX - minX) * 0.1, y0 = minY + (maxY - minY) * 0.1;
            const double width = (maxX - minX) * 0.8, height = (maxY - minY) * 0.8;
            QJsonArray points;
            std::vector<std::array<double, 2>> local;
            const double step = width / teeth;
            for (int i = 0; i < teeth; ++i) {
                const double x = x0 + step * i;
                local.push_back({x, y0});
                local.push_back({x, y0 + height});
                local.push_back({x + step / 2, y0 + height});
                local.push_back({x + step / 2, y0 + height * 0.01});
            }
            local.push_back({x0 + width, y0 + height * 0.01});
            local.push_back({x0 + width, y0 - height * 0.05});
            local.push_back({x0, y0 - height * 0.05});
            for (const auto &p : local) {
                const auto m = world.point({p[0], p[1], 0});
                points.append(QJsonArray{m.x, m.y});
            }
            return std::pair{local, points};
        };
        const auto [hostile, hostilePoints] = comb(4999);
        const QJsonObject refused{{"command", "terrain.sculpt"},
                                  {"terrain", QString::number(terrain)},
                                  {"mode", "smooth"},
                                  {"region", hostilePoints},
                                  {"falloffWidth", 5}};
        std::string code;
        timer.restart();
        try {
            QJsonObject operation;
            executeTerrainCommand(doc, refused, &operation);
        } catch (const TerrainError &error) {
            code = error.code();
        }
        const double refusalMs = ms(timer);
        check(code == "LIMIT_EXCEEDED", "the hostile comb is refused");
        const auto [bounded, boundedPoints] = comb(128);
        auto data = std::make_shared<TerrainSculptData>();
        data->region = bounded;
        data->width = 5;
        data->amount = 0.01;
        std::vector<double> times;
        TerrainEditPreview candidate;
        for (int i = 0; i < 5; ++i) {
            timer.restart();
            candidate = previewTerrainEdit(doc, terrain, terrainSculptKind, data);
            times.push_back(ms(timer));
        }
        result["hostileRegion"] = QJsonObject{
            {"refusedCorners", double(hostile.size())},
            {"refusedBandEdges", double(terrainSculptRegionBands(hostile).edges)},
            {"refusalMs", refusalMs},
            {"boundedCorners", double(bounded.size())},
            {"boundedBandEdges", double(terrainSculptRegionBands(bounded).edges)},
            {"boundedVerticesMoved", double(candidate.liftedVertices)},
            {"boundedTilesTouched", double(candidate.region.replaced.size())},
            {"boundedPreviewMs", stats(times)}};
    }
    while (doc.canUndo() && !doc.bodies().at(terrain)->terrain->edits.empty())
        doc.undo();
    // A replaying Regenerate of sculpt entries (half raise, half three-iteration smooth).
    if (replayEntries > 0) {
        for (int i = 0; i < replayEntries; ++i) {
            const auto &key = bySize[std::size_t(i) % pool].second;
            auto data = stroke(key, 0.25);
            if (i % 2) {
                data->amount = 0;
                data->strength = 0.5;
                data->iterations = 3;
            }
            try {
                applyTerrainEdit(doc, terrain, i % 2 ? terrainSmoothKind : terrainSculptKind, data,
                                 "Sculpt terrain");
            } catch (const TerrainError &) {
                // A smoothing stroke over an already smooth patch may move nothing: skip it.
            }
        }
        const auto entries = doc.bodies().at(terrain)->terrain->edits.size();
        const QJsonObject regenerate{{"command", "terrain.regenerate"},
                                     {"terrain", QString::number(terrain)}};
        timer.restart();
        const auto request = parseTerrainReplayRequest(doc, regenerate);
        const double requestMs = ms(timer);
        timer.restart();
        auto replayed = runTerrainReplay(request);
        const double runMs = ms(timer);
        check(replayed.ok, "the sculpt replay succeeds");
        timer.restart();
        const auto noOp = applyTerrainReplay(doc, request, std::move(replayed));
        const double applyMs = ms(timer);
        check(!noOp["changed"].toBool(), "Regenerate after the sculpts is a no-op");
        result["replay"] = QJsonObject{{"entries", double(entries)},
                                       {"requestMs", requestMs},
                                       {"runMs", runMs},
                                       {"applyMs", applyMs},
                                       {"totalMs", requestMs + runMs + applyMs}};
        while (doc.canUndo() && !doc.bodies().at(terrain)->terrain->edits.empty())
            doc.undo();
    }
    return result;
}

// R096.d: pad stamps (terrain.stamp_pad). Square pads in the middle of the largest tiles, a
// quarter of the tile's smaller side across and 0.2 m above the ground there, so the pad and its
// slopes stay inside about one tile: the preview (previewTerrainEdit: the rays, the walk, the
// repair, the cavity and the adapter), the commit through applyTerrainEdit and through the whole
// command (its dry run committed without planning again), with the display update (decision 10:
// one-tile commit p95 <= 100 ms; samples whose touched tiles hold more than 2,048 triangles are
// reported apart). Then a larger pad (a whole cell across), the 64-vertex footprint of §9 (a
// circle half a cell across), a 20,000-point circle and a refused comb, a pad parameter change
// replaying the list (terrain.update_edit of the first of P pads), and a replaying Regenerate
// of P pad entries (one per tile, default 40, placed while the terrain stays 3,000 triangles under
// the document limit).
QJsonObject measureStamps(Document &doc, Id terrain, int samples, int replayEntries, double slope) {
    QElapsedTimer timer;
    const auto world = doc.worldTransform(terrain);
    const auto grid = doc.bodies().at(terrain)->terrain->tiling;
    std::vector<std::pair<std::size_t, TerrainTileKey>> bySize;
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &tile = *doc.bodies().at(id);
        bySize.push_back({tile.surface.faces.size(), tile.tileKey});
    }
    std::stable_sort(bySize.begin(), bySize.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    auto ground = [&](double x, double y) {
        const auto m = world.point({x, y, 0});
        const auto found = terrainElevationAt(doc, terrain, {{m.x, m.y}});
        check(found.front().found, "a pad centre is on the terrain");
        return world.inverse().point({m.x, m.y, found.front().elevation}).z;
    };
    // A pad in the middle of a tile: `points` corners on a circle of `radius` (4 is a square),
    // `lift` metres above the ground at the centre.
    auto pad = [&](const TerrainTileKey &key, double radius, int points, double lift) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto *tile = mesh->tile(key);
        const double cx = (tile->minX + tile->maxX) / 2, cy = (tile->minY + tile->maxY) / 2;
        auto data = std::make_shared<TerrainStampData>();
        for (int i = 0; i < points; ++i) {
            const double t = 2 * std::numbers::pi * (i + 0.5) / points;
            data->footprint.push_back({cx + radius * std::cos(t), cy + radius * std::sin(t)});
        }
        data->elevation = ground(cx, cy) + lift;
        data->cutSlope = data->fillSlope = slope;
        data->maxDistance = 100;
        return data;
    };
    auto side = [&](const TerrainTileKey &key) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto *tile = mesh->tile(key);
        return std::min(tile->maxX - tile->minX, tile->maxY - tile->minY);
    };
    auto touchedTriangles = [&](const TerrainEditPreview &preview) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        std::size_t triangles = 0;
        for (const auto &key : preview.region.replaced)
            triangles += mesh->tile(key)->content->triangles.size();
        return triangles;
    };
    auto commandOf = [&](const TerrainStampData &data) {
        QJsonArray polygon;
        for (const auto &p : data.footprint) {
            const auto m = world.point({p[0], p[1], 0});
            polygon.append(QJsonArray{m.x, m.y});
        }
        return QJsonObject{{"command", "terrain.stamp_pad"},
                           {"terrain", QString::number(terrain)},
                           {"footprint", QJsonObject{{"polygon", polygon}}},
                           {"elevation", world.point({0, 0, data.elevation}).z},
                           {"cutSlope", slope},
                           {"fillSlope", slope}};
    };
    TerrainSurfaceCache surfaces;
    TerrainContourCache contours;
    (void)surfaces.get(doc, terrain);
    (void)contours.get(doc, terrain);
    std::vector<double> preview, previewOver, core, command, display, commandAndDisplay,
        tilesTouched, triangles, rays, cavity;
    std::size_t over = 0, refused = 0;
    const auto pool = std::min<std::size_t>(bySize.size(), 64);
    for (int i = 0; i < samples; ++i) {
        const auto &key = bySize[std::size_t(i) % pool].second;
        auto data = pad(key, side(key) / 8 * std::sqrt(2.0), 4, 0.2);
        TerrainEditPreview candidate;
        timer.restart();
        try {
            candidate = previewTerrainEdit(doc, terrain, terrainStampKind, data);
        } catch (const TerrainError &) {
            ++refused; // a pad over an earlier one's slopes into its outline, say
            continue;
        }
        const double previewMs = ms(timer);
        const auto touched = touchedTriangles(candidate);
        tilesTouched.push_back(double(candidate.region.replaced.size()));
        triangles.push_back(double(touched));
        cavity.push_back(double(candidate.removedTriangles));
        if (touched > TerrainLimits::tileTriangles) {
            ++over;
            previewOver.push_back(previewMs);
            continue;
        }
        preview.push_back(previewMs);
        if (i % 2 == 0) {
            timer.restart();
            applyTerrainEdit(doc, terrain, terrainStampKind, data, "Stamp pad");
            core.push_back(ms(timer));
        } else {
            QJsonObject operation;
            timer.restart();
            executeTerrainCommand(doc, commandOf(*data), &operation);
            command.push_back(ms(timer));
            rays.push_back(operation["terrainReport"].toObject()["terrain"].toObject()["rays"]
                               .toObject()["count"]
                               .toDouble());
        }
        check(doc.bodies().at(terrain)->terrain->edits.size() == 1, "the sample pad committed");
        QElapsedTimer update;
        update.start();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
        display.push_back(ms(update));
        if (i % 2)
            commandAndDisplay.push_back(command.back() + display.back());
        // Undo, so every sample stamps the same kind of ground.
        doc.undo();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
    }
    QJsonObject result{
        {"oneTile",
         QJsonObject{{"samples", samples},
                     {"overBudgetTiles", double(over)},
                     {"refused", double(refused)},
                     {"tilesTouched", stats(tilesTouched)},
                     {"touchedTriangles", stats(triangles)},
                     {"cavityTriangles", stats(cavity)},
                     {"rays", stats(rays)},
                     {"previewMs", stats(preview)},
                     {"coreCommitMs", stats(core)},
                     {"commandCommitMs", stats(command)},
                     {"displayUpdateMs", stats(display)},
                     {"commandAndDisplayMs", stats(commandAndDisplay)}}}};
    if (!previewOver.empty())
        result["oneTileOverBudgetPreviewMs"] = stats(previewOver);
    // A larger pad (a whole tile side across, 1 m above the ground) and the 64-vertex footprint.
    auto larger = [&](const char *label, double scale, int points, double lift) {
        std::vector<double> previewTimes, commitTimes, tiles, cavityTriangles, rayCounts;
        for (int i = 0; i < std::max(1, samples / 10); ++i) {
            const auto &key = bySize[std::size_t(i * 7) % pool].second;
            auto data = pad(key, side(key) * scale, points, lift);
            try {
                timer.restart();
                const auto candidate = previewTerrainEdit(doc, terrain, terrainStampKind, data);
                previewTimes.push_back(ms(timer));
                tiles.push_back(double(candidate.region.replaced.size()));
                cavityTriangles.push_back(double(candidate.removedTriangles));
                QJsonObject operation;
                timer.restart();
                executeTerrainCommand(doc, commandOf(*data), &operation);
                commitTimes.push_back(ms(timer));
                rayCounts.push_back(operation["terrainReport"].toObject()["terrain"].toObject()["rays"]
                                        .toObject()["count"]
                                        .toDouble());
                doc.undo();
            } catch (const TerrainError &) {
            }
        }
        result[label] = QJsonObject{{"tilesTouched", stats(tiles)},
                                    {"cavityTriangles", stats(cavityTriangles)},
                                    {"rays", stats(rayCounts)},
                                    {"previewMs", stats(previewTimes)},
                                    {"commandCommitMs", stats(commitTimes)}};
    };
    larger("largerPad", 0.5 * std::sqrt(2.0), 4, 1.0);
    larger("footprint64", 0.25, 64, 0.5);
    // Hostile inputs: a 20,000-point circle (applied or refused by a bound) and a comb (refused
    // by the band bound before any work).
    {
        const auto &key = bySize.front().second;
        auto circle = pad(key, side(key) * 0.3, 20000, 0.3);
        std::string code = "applied";
        timer.restart();
        try {
            QJsonObject operation;
            executeTerrainCommand(doc, commandOf(*circle), &operation);
        } catch (const TerrainError &error) {
            code = error.code();
        }
        const double circleMs = ms(timer);
        if (code == "applied")
            doc.undo();
        auto comb = std::make_shared<TerrainStampData>(*circle);
        comb->footprint.clear();
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto *tile = mesh->tile(key);
        for (int i = 0; i < 2000; ++i) {
            const double x0 = tile->minX + (tile->maxX - tile->minX) * i / 2000,
                         x1 = x0 + (tile->maxX - tile->minX) / 4000;
            comb->footprint.push_back({x0, tile->minY + 1});
            comb->footprint.push_back({x0, tile->maxY - 1});
            comb->footprint.push_back({x1, tile->maxY - 1});
            comb->footprint.push_back({x1, tile->minY + 2});
        }
        std::string combCode = "applied";
        timer.restart();
        try {
            QJsonObject operation;
            executeTerrainCommand(doc, commandOf(*comb), &operation);
        } catch (const TerrainError &error) {
            combCode = error.code();
        }
        const double combMs = ms(timer);
        check(combCode == "LIMIT_EXCEEDED", "the comb footprint is refused");
        result["hostile"] = QJsonObject{{"circle20000", QJsonObject{{"outcome", QString::fromStdString(code)},
                                                                   {"ms", circleMs}}},
                                        {"comb8000", QJsonObject{{"outcome", QString::fromStdString(combCode)},
                                                                 {"ms", combMs}}}};
    }
    // P pads, one per tile (largest first), then a parameter change of the first (a replay of
    // the list) and a replaying Regenerate.
    if (replayEntries > 0) {
        std::uint32_t first = 0;
        std::size_t placed = 0;
        // Pads add triangles: placing stops 3,000 below the document limit, so the update and
        // the replay stay within it.
        for (std::size_t i = 0; i < bySize.size() && placed < std::size_t(replayEntries) &&
                                terrainTriangleCount(doc, terrain) + 3000 < TerrainLimits::triangles;
             ++i) {
            const auto &key = bySize[i].second;
            try {
                applyTerrainEdit(doc, terrain, terrainStampKind, pad(key, side(key) / 8, 4, 0.2),
                                 "Stamp pad");
                if (!first)
                    first = doc.bodies().at(terrain)->terrain->edits.back()->id;
                ++placed;
            } catch (const TerrainError &) {
                // Its slopes would enter a neighbour's pad: skip that tile.
            }
        }
        const auto entries = doc.bodies().at(terrain)->terrain->edits.size();
        const auto &firstData = dynamic_cast<const TerrainStampData &>(
            *doc.bodies().at(terrain)->terrain->edits.front()->data);
        const QJsonObject update{{"command", "terrain.update_edit"},
                                 {"terrain", QString::number(terrain)},
                                 {"edit", double(first)},
                                 {"elevation", world.point({0, 0, firstData.elevation + 0.1}).z}};
        timer.restart();
        QJsonObject operation;
        executeTerrainCommand(doc, update, &operation);
        const double updateMs = ms(timer);
        const QJsonObject regenerate{{"command", "terrain.regenerate"},
                                     {"terrain", QString::number(terrain)}};
        timer.restart();
        const auto request = parseTerrainReplayRequest(doc, regenerate);
        const double requestMs = ms(timer);
        timer.restart();
        auto replayed = runTerrainReplay(request);
        const double runMs = ms(timer);
        check(replayed.ok, "the pad replay succeeds");
        timer.restart();
        const auto noOp = applyTerrainReplay(doc, request, std::move(replayed));
        const double applyMs = ms(timer);
        check(!noOp["changed"].toBool(), "Regenerate after the pads is a no-op");
        const auto pads = checkTerrainPadConsistency(doc, terrain);
        const auto sources = checkTerrainSourceConsistency(doc, terrain);
        const auto seams = checkTerrainSeams(doc, terrain);
        check(pads.ok() && sources.ok() && seams.ok(), "C1, C3 and C6 hold after the pads");
        result["replay"] = QJsonObject{{"entries", double(entries)},
                                       {"updateFirstMs", updateMs},
                                       {"requestMs", requestMs},
                                       {"runMs", runMs},
                                       {"applyMs", applyMs},
                                       {"totalMs", requestMs + runMs + applyMs},
                                       {"padRaysChecked", double(pads.rays)}};
        while (doc.canUndo() && !doc.bodies().at(terrain)->terrain->edits.empty())
            doc.undo();
    }
    return result;
}

// R096.e: drapes (terrain.drape). A three-point line across the middle of one of the largest
// tiles (60 % of its smaller side, crossing it diagonally): the preview (previewTerrainEdit: the
// arrangement, the split against the mesh, the per-triangle re-triangulation and the candidate
// tiles), the commit through applyTerrainEdit and through the whole command (model edges, the
// dry run committed without planning again), with the display update (decision 10: one-tile
// commit p95 <= 100 ms; samples whose touched tiles hold more than 2,048 triangles are reported
// apart). Then a long line across the whole terrain (corner to corner, reported), a painted face
// over several tiles, the overlay of a one-tile face, and a replaying Regenerate of R drape
// entries (one per tile, default 100, placed while the terrain stays 3,000 triangles under the
// document limit).
QJsonObject measureDrapes(Document &doc, Id terrain, int samples, int replayEntries) {
    QElapsedTimer timer;
    const auto world = doc.worldTransform(terrain);
    std::vector<std::pair<std::size_t, TerrainTileKey>> bySize;
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &tile = *doc.bodies().at(id);
        bySize.push_back({tile.surface.faces.size(), tile.tileKey});
    }
    std::stable_sort(bySize.begin(), bySize.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    using Plan2 = std::array<double, 2>;
    auto lineIn = [&](const TerrainTileKey &key, double fraction, int variant) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto *tile = mesh->tile(key);
        const double cx = (tile->minX + tile->maxX) / 2, cy = (tile->minY + tile->maxY) / 2;
        const double r = std::min(tile->maxX - tile->minX, tile->maxY - tile->minY) * fraction / 2;
        const double t = 0.37 + 0.11 * variant;
        return std::vector<Plan2>{{cx - r * std::cos(t), cy - r * std::sin(t)},
                                  {cx + 0.13 * r, cy - 0.21 * r},
                                  {cx + r * std::cos(t), cy + r * std::sin(t)}};
    };
    auto dataOf = [](const std::vector<Plan2> &line, bool closed = false) {
        auto data = std::make_shared<TerrainDrapeData>();
        data->lines.push_back({line, closed});
        return data;
    };
    // Model edges for a plan line (a loose polyline; its height does not matter).
    auto edgesFor = [&](const std::vector<Plan2> &line) {
        std::vector<Vec3> points;
        for (const auto &p : line)
            points.push_back(world.point({p[0], p[1], 50}));
        const Id body = doc.addWire(0, points[0], points[1]);
        for (std::size_t i = 1; i + 1 < points.size(); ++i)
            doc.addWire(body, points[i], points[i + 1]);
        QJsonArray edges;
        for (const auto &[edge, record] : doc.bodies().at(body)->topology.edges)
            edges.append(QJsonObject{{"body", QString::number(body)}, {"edge", QString::number(edge)}});
        return edges;
    };
    auto touchedTriangles = [&](const TerrainEditPreview &preview) {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        std::size_t triangles = 0;
        for (const auto &key : preview.region.replaced)
            triangles += mesh->tile(key)->content->triangles.size();
        return triangles;
    };
    TerrainSurfaceCache surfaces;
    TerrainContourCache contours;
    (void)surfaces.get(doc, terrain);
    (void)contours.get(doc, terrain);
    std::vector<double> preview, previewOver, core, command, display, commandAndDisplay,
        tilesTouched, triangles, split;
    std::size_t over = 0, refused = 0;
    const auto pool = std::min<std::size_t>(bySize.size(), 64);
    for (int i = 0; i < samples; ++i) {
        const auto &key = bySize[std::size_t(i) % pool].second;
        const auto line = lineIn(key, 0.6, i / int(pool));
        auto data = dataOf(line);
        TerrainEditPreview candidate;
        timer.restart();
        try {
            candidate = previewTerrainEdit(doc, terrain, terrainDrapeKind, data);
        } catch (const TerrainError &) {
            ++refused;
            continue;
        }
        const double previewMs = ms(timer);
        const auto touched = touchedTriangles(candidate);
        tilesTouched.push_back(double(candidate.region.replaced.size()));
        triangles.push_back(double(touched));
        split.push_back(double(candidate.removedTriangles));
        if (touched > TerrainLimits::tileTriangles) {
            ++over;
            previewOver.push_back(previewMs);
            continue;
        }
        preview.push_back(previewMs);
        bool wires = false;
        if (i % 2 == 0) {
            timer.restart();
            applyTerrainEdit(doc, terrain, terrainDrapeKind, data, "Drape on terrain");
            core.push_back(ms(timer));
        } else {
            const auto edges = edgesFor(line);
            wires = true;
            QJsonObject operation;
            timer.restart();
            executeTerrainCommand(doc, QJsonObject{{"command", "terrain.drape"},
                                                   {"terrain", QString::number(terrain)},
                                                   {"mode", "imprint"},
                                                   {"edges", edges}},
                                  &operation);
            command.push_back(ms(timer));
        }
        check(doc.bodies().at(terrain)->terrain->edits.size() == 1, "the sample drape committed");
        QElapsedTimer update;
        update.start();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
        display.push_back(ms(update));
        if (i % 2)
            commandAndDisplay.push_back(command.back() + display.back());
        doc.undo();
        if (wires)
            doc.undo();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
    }
    QJsonObject result{
        {"oneTile",
         QJsonObject{{"samples", samples},
                     {"overBudgetTiles", double(over)},
                     {"refused", double(refused)},
                     {"tilesTouched", stats(tilesTouched)},
                     {"touchedTriangles", stats(triangles)},
                     {"trianglesSplit", stats(split)},
                     {"previewMs", stats(preview)},
                     {"coreCommitMs", stats(core)},
                     {"commandCommitMs", stats(command)},
                     {"displayUpdateMs", stats(display)},
                     {"commandAndDisplayMs", stats(commandAndDisplay)}}}};
    if (!previewOver.empty())
        result["oneTileOverBudgetPreviewMs"] = stats(previewOver);
    // A long line across the whole terrain, corner to corner (inset 1 %), as one command.
    {
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        for (const auto *tile : mesh->tiles()) {
            minX = std::min(minX, tile->minX);
            minY = std::min(minY, tile->minY);
            maxX = std::max(maxX, tile->maxX);
            maxY = std::max(maxY, tile->maxY);
        }
        const double dx = maxX - minX, dy = maxY - minY;
        const std::vector<Plan2> line{{minX + 0.2 * dx, minY + 0.03 * dy},
                                      {minX + 0.5 * dx + 0.37, minY + 0.5 * dy + 0.21},
                                      {minX + 0.8 * dx, minY + 0.97 * dy}};
        timer.restart();
        const auto candidate = previewTerrainEdit(doc, terrain, terrainDrapeKind, dataOf(line));
        const double previewMs = ms(timer);
        const auto edges = edgesFor(line);
        QJsonObject operation;
        timer.restart();
        executeTerrainCommand(doc, QJsonObject{{"command", "terrain.drape"},
                                               {"terrain", QString::number(terrain)},
                                               {"mode", "imprint"},
                                               {"edges", edges}},
                              &operation);
        const double commandMs = ms(timer);
        QElapsedTimer update;
        update.start();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
        const double displayMs = ms(update);
        const auto report = operation["terrainReport"].toObject()["terrain"].toObject();
        const auto sources = checkTerrainSourceConsistency(doc, terrain);
        const auto seams = checkTerrainSeams(doc, terrain);
        check(sources.ok() && seams.ok(), "C1 and C6 hold after the long line");
        result["longLine"] = QJsonObject{{"lengthM", std::hypot(0.6 * dx, 0.94 * dy)},
                                         {"tilesReplaced", report["tilesReplaced"]},
                                         {"trianglesSplit", report["trianglesSplit"]},
                                         {"trianglesAdded", report["trianglesAdded"]},
                                         {"points", report["points"]},
                                         {"previewMs", previewMs},
                                         {"commandCommitMs", commandMs},
                                         {"displayUpdateMs", displayMs},
                                         {"candidateTiles", double(candidate.region.tiles.size())}};
        doc.undo();
        doc.undo();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
    }
    // A painted face over several tiles (two cells across), and the overlay of a one-tile face.
    {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto *tile = mesh->tile(bySize.front().second);
        const double cx = (tile->minX + tile->maxX) / 2, cy = (tile->minY + tile->maxY) / 2;
        const double cell = doc.bodies().at(terrain)->terrain->tiling.cell;
        auto faceAt = [&](double half) {
            const std::vector<Vec3> loop{world.point({cx - half, cy - half * 0.8, 60}),
                                         world.point({cx + half, cy - half * 0.8, 60}),
                                         world.point({cx + half, cy + half * 0.8, 60}),
                                         world.point({cx - half, cy + half * 0.8, 60})};
            const auto face = doc.addFace({loop}, "Drape face");
            return QJsonArray{QJsonObject{
                {"body", QString::number(face)},
                {"face", QString::number(doc.bodies().at(face)->surface.faces.begin()->first)}}};
        };
        Id material = 0;
        for (const auto &[id, record] : doc.materials())
            material = id;
        if (!material) {
            Edit edit{"Material", {}};
            material = doc.nextMaterialId();
            edit.materials.push_back({material, nullptr,
                                      std::make_shared<MaterialRecord>(MaterialRecord{material, "Gravel", {0.6f, 0.5f, 0.4f}, 1, 0})});
            doc.apply(std::move(edit), doc.revision());
        }
        const auto faces = faceAt(cell * 0.9);
        {
            const auto body = faces.at(0).toObject()["body"].toString().toULongLong();
            auto painted = std::make_shared<Body>(*doc.bodies().at(body));
            painted->materials = {material, material};
            doc.apply({"Paint", {{body, doc.bodies().at(body), painted}}}, doc.revision());
        }
        QJsonObject operation;
        timer.restart();
        executeTerrainCommand(doc, QJsonObject{{"command", "terrain.drape"},
                                               {"terrain", QString::number(terrain)},
                                               {"mode", "imprint"},
                                               {"faces", faces},
                                               {"paintRegions", true}},
                              &operation);
        const double paintMs = ms(timer);
        const auto report = operation["terrainReport"].toObject()["terrain"].toObject();
        timer.restart();
        const auto request = parseTerrainReplayRequest(doc, QJsonObject{{"command", "terrain.regenerate"},
                                                                        {"terrain", QString::number(terrain)}});
        auto replayed = runTerrainReplay(request);
        check(replayed.ok, "the painted drape replays");
        const auto noOp = applyTerrainReplay(doc, request, std::move(replayed));
        const double regenerateMs = ms(timer);
        check(!noOp["changed"].toBool(), "Regenerate after the painted drape is a no-op");
        result["paintedFace"] = QJsonObject{{"tilesReplaced", report["tilesReplaced"]},
                                            {"tilesRepainted", report["tilesRepainted"]},
                                            {"trianglesSplit", report["trianglesSplit"]},
                                            {"commandCommitMs", paintMs},
                                            {"regenerateMs", regenerateMs}};
        doc.undo();
        doc.undo();
        doc.undo();
        const auto small = faceAt(std::min(tile->maxX - tile->minX, tile->maxY - tile->minY) * 0.3);
        operation = {};
        timer.restart();
        executeTerrainCommand(doc, QJsonObject{{"command", "terrain.drape"},
                                               {"terrain", QString::number(terrain)},
                                               {"mode", "overlay"},
                                               {"faces", small}},
                              &operation);
        result["overlayOneTile"] = QJsonObject{{"faces", operation["faces"]},
                                               {"edges", operation["edges"]},
                                               {"commandMs", ms(timer)}};
        doc.undo();
        doc.undo();
    }
    // R drapes, one per tile (largest first), then a replaying Regenerate.
    if (replayEntries > 0) {
        std::size_t placed = 0;
        for (std::size_t i = 0; i < bySize.size() && placed < std::size_t(replayEntries) &&
                                terrainTriangleCount(doc, terrain) + 3000 < TerrainLimits::triangles;
             ++i) {
            try {
                applyTerrainEdit(doc, terrain, terrainDrapeKind, dataOf(lineIn(bySize[i].second, 0.6, 0)),
                                 "Drape on terrain");
                ++placed;
            } catch (const TerrainError &) {
            }
        }
        const QJsonObject regenerate{{"command", "terrain.regenerate"},
                                     {"terrain", QString::number(terrain)}};
        timer.restart();
        const auto request = parseTerrainReplayRequest(doc, regenerate);
        const double requestMs = ms(timer);
        timer.restart();
        auto replayed = runTerrainReplay(request);
        const double runMs = ms(timer);
        check(replayed.ok, "the drape replay succeeds");
        timer.restart();
        const auto noOp = applyTerrainReplay(doc, request, std::move(replayed));
        const double applyMs = ms(timer);
        check(!noOp["changed"].toBool(), "Regenerate after the drapes is a no-op");
        const auto sources = checkTerrainSourceConsistency(doc, terrain);
        const auto seams = checkTerrainSeams(doc, terrain);
        check(sources.ok() && seams.ok(), "C1 and C6 hold after the drapes");
        result["replay"] = QJsonObject{{"entries", double(placed)},
                                       {"requestMs", requestMs},
                                       {"runMs", runMs},
                                       {"applyMs", applyMs},
                                       {"totalMs", requestMs + runMs + applyMs}};
        while (doc.canUndo() && !doc.bodies().at(terrain)->terrain->edits.empty())
            doc.undo();
    }
    return result;
}

QJsonObject measure(const char *name, const QByteArray &bytes, const QJsonObject &command,
                    int edits, int replayEntries, int cancelTrials, int sourceEdits, int sculpts,
                    int sculptReplay) {
    Document doc;
    auto request = parseTerrainSourceRequest(bytes, command);
    auto result = runTerrainImport(request.set, request.options, doc.site(), request.provenance);
    check(result.ok, std::string(name) + " imports: " + result.message);
    const auto applied = applyTerrainImport(doc, std::move(result), request.name);
    const Id terrain = applied["terrain"].toString().toULongLong();
    const auto triangles = terrainTriangleCount(doc, terrain);
    const auto sourceEditResult =
        sourceEdits > 0 ? measureSourceEdits(doc, terrain, sourceEdits) : QJsonObject{};
    const auto sculptResult =
        sculpts > 0 ? measureSculpts(doc, terrain, sculpts, sculptReplay) : QJsonObject{};

    {
        auto display = doc.bodies().at(terrain)->terrain->display;
        display.contours = true;
        setTerrainDisplay(doc, terrain, display);
    }
    TerrainSurfaceCache surfaces;
    TerrainContourCache contours;
    QElapsedTimer coldDisplay;
    coldDisplay.start();
    (void)surfaces.get(doc, terrain);
    (void)contours.get(doc, terrain);
    const double coldDisplayMs = ms(coldDisplay);
    const auto recordBytes = terrainRecordBytes(*doc.bodies().at(terrain)->terrain);

    // The edit mesh: cold build (grid index and topology) and memory.
    QElapsedTimer timer;
    terrainEditMeshCache().clear();
    timer.start();
    const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
    const double coldBuild = ms(timer);
    const auto meshBytes = mesh->memoryBytes();

    // Elevation: 200 points, indexed (warm cache) against R095's full scan.
    std::vector<std::array<double, 2>> probes;
    const auto grid = doc.bodies().at(terrain)->terrain->tiling;
    for (int i = 0; i < 200; ++i)
        probes.push_back({grid.originX + (i * 37 % 200) * 2.5, grid.originY + (i * 53 % 200) * 2.5});
    std::vector<double> indexed;
    for (int run = 0; run < 5; ++run) {
        timer.restart();
        (void)terrainElevationAt(doc, terrain, probes);
        indexed.push_back(ms(timer));
    }

    // Tiles to edit: the largest tiles first, spread over the terrain.
    std::vector<std::pair<std::size_t, TerrainTileKey>> bySize;
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &tile = *doc.bodies().at(id);
        bySize.push_back({tile.surface.faces.size(), tile.tileKey});
    }
    std::stable_sort(bySize.begin(), bySize.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<double> commit, phaseKind, phaseReplace, phaseApply, digest, cacheUpdate,
        displayUpdate, commitAndDisplay;
    std::size_t largestCharge = 0, editedTriangles = 0;
    const auto historyBefore = doc.terrainHistoryBytes();
    for (int i = 0; i < edits; ++i) {
        const auto &[size, key] = bySize[std::size_t(i) % std::min<std::size_t>(bySize.size(), 64)];
        const auto charged = doc.terrainHistoryBytes();
        timer.restart();
        applyTerrainEdit(doc, terrain, "test.lift", kinds::lift(key, .01), "Lift tile");
        commit.push_back(ms(timer));
        QElapsedTimer display;
        display.start();
        (void)surfaces.get(doc, terrain);
        (void)contours.get(doc, terrain);
        displayUpdate.push_back(ms(display));
        commitAndDisplay.push_back(commit.back() + displayUpdate.back());
        largestCharge = std::max(largestCharge, doc.terrainHistoryBytes() - charged);
        editedTriangles = std::max(editedTriangles, size);
        if (i < 20) {
            // The phases of the same kind of commit, timed separately on the next state.
            const auto &[nextSize, nextKey] = bySize[std::size_t(i + 1) % bySize.size()];
            (void)nextSize;
            timer.restart();
            const auto current = terrainEditMeshCache().get(doc, terrain, true);
            cacheUpdate.push_back(ms(timer));
            TerrainEdit entry{1, "test.lift", kinds::lift(nextKey, .01)};
            timer.restart();
            const auto outcome = kinds::applyLift(*current, entry);
            phaseKind.push_back(ms(timer));
            timer.restart();
            const auto region = replaceTerrainRegion(*current, *outcome.change);
            phaseReplace.push_back(ms(timer));
            timer.restart();
            (void)terrainMeshDigest(doc, terrain);
            digest.push_back(ms(timer));
        }
    }
    const auto historyCharge = doc.terrainHistoryBytes() - historyBefore;
    const double perEdit = double(historyCharge) / edits;
    timer.restart();
    const auto seams = checkTerrainSeams(doc, terrain);
    const double seamTime = ms(timer);
    check(seams.ok(), std::string(name) + ": seams hold after the edits");
    std::size_t undone = 0;
    while (doc.canUndo() && undone < std::size_t(edits)) {
        doc.undo();
        ++undone;
    }
    check(undone == std::size_t(edits), std::string(name) + ": every edit is undoable");

    // The replaying Regenerate: M lifts, then the request, the run and the apply.
    for (int i = 0; i < replayEntries; ++i) {
        const auto &[size, key] = bySize[std::size_t(i) % std::min<std::size_t>(bySize.size(), 64)];
        applyTerrainEdit(doc, terrain, "test.lift", kinds::lift(key, .001 * ((i % 7) + 1)),
                         "Lift tile");
    }
    const QJsonObject regenerate{{"command", "terrain.regenerate"},
                                 {"terrain", QString::number(terrain)}};
    timer.restart();
    const auto replayRequest = parseTerrainReplayRequest(doc, regenerate);
    const double requestMs = ms(timer);
    timer.restart();
    auto replayed = runTerrainReplay(replayRequest);
    const double runMs = ms(timer);
    check(replayed.ok, std::string(name) + ": the replay succeeds");
    timer.restart();
    const auto noOp = applyTerrainReplay(doc, replayRequest, std::move(replayed));
    const double applyMs = ms(timer);
    check(!noOp["changed"].toBool(), std::string(name) + ": Regenerate after edits is a no-op");
    // Cancel latency: from the request (another thread) until the run has returned.
    std::vector<double> latency;
    for (int trial = 0; trial < cancelTrials; ++trial) {
        std::atomic<bool> cancel{false};
        std::atomic<std::int64_t> requested{0};
        TerrainProgress progress([&] { return cancel.load(); });
        QElapsedTimer clock;
        clock.start();
        const auto delay = std::chrono::microseconds(
            std::int64_t(runMs * 1000 * (trial + .5) / double(std::max(cancelTrials, 1))));
        std::thread canceller([&] {
            std::this_thread::sleep_for(delay);
            requested = clock.nsecsElapsed();
            cancel = true;
        });
        const auto result = runTerrainReplay(replayRequest, &progress);
        const auto returned = clock.nsecsElapsed();
        canceller.join();
        if (result.cancelled)
            latency.push_back(double(returned - requested) / 1e6);
    }
    QJsonObject replay{{"entries", replayEntries},
                       {"requestMs", requestMs},
                       {"runMs", runMs},
                       {"applyMs", applyMs},
                       {"totalMs", requestMs + runMs + applyMs}};
    if (!latency.empty())
        replay["cancelLatencyMs"] = stats(latency);
    return {{"name", name},
            {"triangles", double(triangles)},
            {"tiles", double(bySize.size())},
            {"recordBytes", double(recordBytes)},
            {"editedTileTriangles", double(editedTriangles)},
            {"commitMs", stats(commit)},
            {"displayUpdateMs", stats(displayUpdate)},
            {"commitAndDisplayMs", stats(commitAndDisplay)},
            {"coldDisplayMs", coldDisplayMs},
            {"replay", replay},
            {"phasesMs", QJsonObject{{"cacheUpdate", stats(cacheUpdate)},
                                     {"kind", stats(phaseKind)},
                                     {"replaceRegion", stats(phaseReplace)},
                                     {"meshDigest", stats(digest)}}},
            {"editMesh", QJsonObject{{"coldBuildMs", coldBuild},
                                     {"memoryBytes", double(meshBytes)},
                                     {"mergedVertices", double(mesh->mergedVertexCount())}}},
            {"elevationAt200Ms", stats(indexed)},
            {"history", QJsonObject{{"edits", edits},
                                    {"chargedBytes", double(historyCharge)},
                                    {"largestEditBytes", double(largestCharge)},
                                    {"editsPer512MiB", double(Document::terrainHistoryLimit) / perEdit},
                                    {"undone", double(undone)}}},
            {"seamCheckMs", seamTime},
            {"sourceEdits", sourceEditResult},
            {"sculpts", sculptResult}};
}
// R096.d: stamps add triangles (a pad's outline and daylight points), and the two terrains above
// are at the document's 250,000-triangle limit, so the stamps are measured on the same terrains
// built from 2 % fewer points or rings (about 245,000 triangles), leaving room for the pads.
QJsonObject measureStampTerrain(const char *name, const QByteArray &bytes,
                                const QJsonObject &command, int stamps, int stampReplay,
                                double slope, int drapes = 0, int drapeReplay = 0) {
    Document doc;
    auto request = parseTerrainSourceRequest(bytes, command);
    auto result = runTerrainImport(request.set, request.options, doc.site(), request.provenance);
    check(result.ok, std::string(name) + " imports: " + result.message);
    const auto applied = applyTerrainImport(doc, std::move(result), request.name);
    const Id terrain = applied["terrain"].toString().toULongLong();
    {
        auto display = doc.bodies().at(terrain)->terrain->display;
        display.contours = true;
        setTerrainDisplay(doc, terrain, display);
    }
    auto measured = stamps > 0 ? measureStamps(doc, terrain, stamps, stampReplay, slope) : QJsonObject{};
    if (drapes > 0)
        measured["drapes"] = measureDrapes(doc, terrain, drapes, drapeReplay);
    measured["name"] = name;
    measured["triangles"] = double(terrainTriangleCount(doc, terrain));
    measured["tiles"] = double(terrainTiles(doc, terrain).size());
    measured["slope"] = slope;
    return measured;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int edits = 200, replay = 1000, cancel = 20, sourceEdits = 100, sculpts = 100,
        sculptReplay = 1000, stamps = 100, stampReplay = 40, drapes = 100, drapeReplay = 100;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--edits" && i + 1 < argc)
            edits = std::max(1, std::atoi(argv[++i]));
        else if (arg == "--replay" && i + 1 < argc)
            replay = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--cancel" && i + 1 < argc)
            cancel = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--source-edits" && i + 1 < argc)
            sourceEdits = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--sculpts" && i + 1 < argc)
            sculpts = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--sculpt-replay" && i + 1 < argc)
            sculptReplay = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--stamps" && i + 1 < argc)
            stamps = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--stamp-replay" && i + 1 < argc)
            stampReplay = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--drapes" && i + 1 < argc)
            drapes = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--drape-replay" && i + 1 < argc)
            drapeReplay = std::max(0, std::atoi(argv[++i]));
    }
    kinds::registerKinds();
    try {
        QJsonArray terrains;
        terrains.append(measure("points", pointSource(125000),
                                {{"format", "points"},
                                 {"units", "m"},
                                 {"columns", "XYZ"},
                                 {"delimiter", "whitespace"},
                                 {"header", false}},
                                edits, replay, cancel, sourceEdits, sculpts, sculptReplay));
        terrains.append(measure("contours", contourSource(416, 300),
                                {{"format", "dxf"}, {"units", "header"}}, edits, replay,
                                cancel, sourceEdits, sculpts, sculptReplay));
        QJsonArray stampTerrains;
        if (stamps > 0 || drapes > 0) {
            stampTerrains.append(measureStampTerrain("points", pointSource(122500),
                                                     {{"format", "points"},
                                                      {"units", "m"},
                                                      {"columns", "XYZ"},
                                                      {"delimiter", "whitespace"},
                                                      {"header", false}},
                                                     stamps, stampReplay, 0.5, drapes, drapeReplay));
            // Its ground falls 0.55 a metre, so the pads grade at 1:1.
            stampTerrains.append(measureStampTerrain("contours", contourSource(408, 300),
                                                     {{"format", "dxf"}, {"units", "header"}},
                                                     stamps, stampReplay, 1.0, drapes, drapeReplay));
        }
        QJsonObject out{{"benchmark", "terrain_edit_benchmark"},
#ifdef SKETCHYUP_BENCHMARK_BUILD_TYPE
                        {"buildType", SKETCHYUP_BENCHMARK_BUILD_TYPE},
#endif
                        {"cpus", int(::sysconf(_SC_NPROCESSORS_ONLN))},
                        {"budgetMs", 100},
                        {"terrains", terrains},
                        {"stamps", stampTerrains}};
        std::cout << QJsonDocument(out).toJson().toStdString();
    } catch (const std::exception &error) {
        std::cerr << "terrain_edit_benchmark: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
