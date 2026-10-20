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
// Uploading to the GPU and drawing the frame are not included (no viewport here). Prints one
// JSON document. Build it Release.
//
//   terrain_edit_benchmark [--edits N] [--replay M] [--cancel TRIALS] [--source-edits K]
#include "automation/terrain_commands.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_source_edit.hpp"
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

QJsonObject measure(const char *name, const QByteArray &bytes, const QJsonObject &command,
                    int edits, int replayEntries, int cancelTrials, int sourceEdits) {
    Document doc;
    auto request = parseTerrainSourceRequest(bytes, command);
    auto result = runTerrainImport(request.set, request.options, doc.site(), request.provenance);
    check(result.ok, std::string(name) + " imports: " + result.message);
    const auto applied = applyTerrainImport(doc, std::move(result), request.name);
    const Id terrain = applied["terrain"].toString().toULongLong();
    const auto triangles = terrainTriangleCount(doc, terrain);
    const auto sourceEditResult =
        sourceEdits > 0 ? measureSourceEdits(doc, terrain, sourceEdits) : QJsonObject{};
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
            {"sourceEdits", sourceEditResult}};
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int edits = 200, replay = 1000, cancel = 20, sourceEdits = 100;
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
                                edits, replay, cancel, sourceEdits));
        terrains.append(measure("contours", contourSource(416, 300),
                                {{"format", "dxf"}, {"units", "header"}}, edits, replay,
                                cancel, sourceEdits));
        QJsonObject out{{"benchmark", "terrain_edit_benchmark"},
#ifdef SKETCHYUP_BENCHMARK_BUILD_TYPE
                        {"buildType", SKETCHYUP_BENCHMARK_BUILD_TYPE},
#endif
                        {"cpus", int(::sysconf(_SC_NPROCESSORS_ONLN))},
                        {"budgetMs", 100},
                        {"terrains", terrains}};
        std::cout << QJsonDocument(out).toJson().toStdString();
    } catch (const std::exception &error) {
        std::cerr << "terrain_edit_benchmark: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
