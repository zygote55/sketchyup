// R095.f (ADR 0168 section 6, "Budgets"): measures the 250,000-triangle terrain budgets.
//
//   terrain_budget_benchmark [--repeat N] [--frame] [--cancel [TRIALS]]
//
// For two generated sources at the triangle limit (a random point file and a contour DXF) it
// times, best of N (default 3), the import dry run as the dialog runs it (parse, preparation and
// triangulation: parseTerrainSourceRequest + runTerrainImport), the commit (applyTerrainImport:
// the one "Import terrain" edit with its tiles and validation), and the save and open of the
// document against the same document without the terrain; it reports the resident memory the
// terrain adds and the file growth. With --frame it also opens a 1920 x 1080 viewport on the
// point-file terrain and times 50 complete frames (Viewport::paintGL plus glFinish, as
// real_model_benchmark does for M9) with the default display and with contour lines and
// elevation bands. Prints one JSON document. Build it Release; run the frame part under the
// platform and renderer to be measured (for example xvfb-run with LIBGL_ALWAYS_SOFTWARE=1).
//
// R096.f1 (ADR 0169 §5, §9): --sculpt-frames [N] measures the terrain preview path from the input
// event to the presented frame instead, on both sources, each in a 20-box model, at 1920 x 1080
// with 1 m contour lines shown, in the Sculpt tool, top view framing each stroke's
// neighbourhood. For N strokes (default 64) through the middle
// of the largest tiles (a radius of a quarter of the tile's smaller side, 0.5 m), it times:
// - preview: each pointer move of the drag that adds a stroke sample, from the event sent to the
//   viewport until the frame showing the candidate is complete (event handling with the plan on
//   the cached edit mesh, then a frame: the candidate's display, its upload and the draw, plus
//   glFinish and the frame's readback);
// - commit: the release, from the event until the frame showing the committed tiles is complete
//   (terrain.sculpt through the batch, the document edit, then the frame with the rebuilt tiles,
//   their seam ring and their contours);
// strokes whose touched tiles hold more than 2,048 triangles in all are reported apart (the
// budget is the one-tile row); each stroke is undone afterwards (outside the timing). It also
// times a nine-tile brush: a click at a tile's middle, radius 1.1 cells, then a drag.
//
// D-044: --cancel measures the cancel latency of the same dry run instead (parse, prepare,
// triangulate and tile with a TerrainProgress): it checks that a run with progress gives the
// same mesh, then cancels TRIALS runs (default 100) at times spread evenly over the dry run,
// from another thread, and reports the time from the cancel request until the run has
// returned, overall and by the stage the run was in.
#include "app/surface_format.hpp"
#include "app/terrain_section.hpp"
#include "app/viewport.hpp"
#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <iostream>
#include <memory>
#include <malloc.h>
#include <numbers>
#include <sys/resource.h>
#include <unistd.h>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
double p95(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples.at(std::size_t(std::ceil(double(samples.size()) * .95)) - 1);
}
double median(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples.at(samples.size() / 2);
}
qint64 residentBytes() {
    QFile status("/proc/self/statm");
    check(status.open(QIODevice::ReadOnly), "Read /proc/self/statm");
    const auto columns = status.readAll().simplified().split(' ');
    return columns.at(1).toLongLong() * ::sysconf(_SC_PAGESIZE);
}
qint64 peakResident() {
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage");
    return qint64(usage.ru_maxrss) * 1024;
}
double mib(qint64 bytes) { return double(bytes) / (1024.0 * 1024.0); }
// The container's chunks (manifest entries with their sizes), and the size of the terrain
// record (sources, parameters, provenance) inside the document chunk.
QJsonObject containerBreakdown(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read the saved file");
    const auto bytes = file.readAll();
    const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
    const auto manifest = QJsonDocument::fromJson(bytes.mid(16, length)).object();
    QJsonArray chunks;
    qint64 recordBytes = 0;
    for (const auto &value : manifest["chunks"].toArray()) {
        const auto entry = value.toObject();
        chunks.append(QJsonObject{{"name", entry["name"]},
                                  {"format", entry["format"]},
                                  {"bytes", entry["bytes"].toString().toDouble()}});
        if (chunks.size() == 1) {
            const auto tree = QJsonDocument::fromJson(
                bytes.mid(16 + length + entry["offset"].toString().toLongLong(),
                          entry["bytes"].toString().toLongLong())).object();
            const auto bodies = tree["bodies"];
            const auto list = bodies.isArray() ? bodies.toArray() : QJsonArray{};
            for (const auto &body : list)
                if (body.toObject().contains("terrain"))
                    recordBytes += QJsonDocument(body.toObject()["terrain"].toObject())
                                       .toJson(QJsonDocument::Compact)
                                       .size();
        }
    }
    return {{"chunks", chunks}, {"terrainRecordJsonBytes", double(recordBytes)}};
}

// About 125,000 random points at UTM scale: Delaunay gives 2n - hull - 2 triangles, just under
// the 250,000 limit.
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
// Concentric contour rings (every segment a constraint), metres, as a minimal DXF.
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

// A small ordinary model: the "same document without terrain".
Document baseDocument() {
    Document doc;
    for (int i = 0; i < 20; ++i) {
        const double x = 3.0 * i;
        const auto body = doc.addFace({{{x, 0, 0}, {x + 2, 0, 0}, {x + 2, 2, 0}, {x, 2, 0}}});
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 2.5);
    }
    return doc;
}

struct Measured {
    QJsonObject json;
    std::optional<Document> document; // the last committed document, for the frame
};

Measured measure(const char *name, const QByteArray &bytes, const QJsonObject &command, int repeat,
                 const QTemporaryDir &files) {
    std::vector<double> parse, triangulate, dryRun, commit, saveWith, saveWithout, openWith,
        openWithout;
    qint64 added = 0, peakDuring = 0, withoutBytes = 0, withBytes = 0;
    int triangles = 0, tiles = 0;
    std::optional<Document> last;
    const auto without = files.filePath(QString(name) + "-without.sketchyup");
    const auto with = files.filePath(QString(name) + "-with.sketchyup");
    for (int run = 0; run < repeat; ++run) {
        last.reset();
        malloc_trim(0);
        auto doc = baseDocument();
        // Without terrain: save and open.
        QElapsedTimer timer;
        timer.start();
        saveDocument(doc, without);
        saveWithout.push_back(double(timer.nsecsElapsed()) / 1e6);
        timer.restart();
        (void)loadDocument(without);
        openWithout.push_back(double(timer.nsecsElapsed()) / 1e6);
        malloc_trim(0);
        const auto residentBefore = residentBytes();
        // The dialog's dry run, then the commit.
        timer.restart();
        auto request = parseTerrainSourceRequest(bytes, command);
        const double parsed = double(timer.nsecsElapsed()) / 1e6;
        auto result = runTerrainImport(request.set, request.options, doc.site(), request.provenance);
        const double total = double(timer.nsecsElapsed()) / 1e6;
        check(result.ok, std::string(name) + " imports: " + result.message);
        parse.push_back(parsed);
        triangulate.push_back(total - parsed);
        dryRun.push_back(total);
        triangles = int(result.mesh.triangles.size());
        timer.restart();
        const auto applied = applyTerrainImport(doc, std::move(result), request.name);
        commit.push_back(double(timer.nsecsElapsed()) / 1e6);
        tiles = int(terrainTiles(doc, applied["terrain"].toString().toULongLong()).size());
        request = {};
        malloc_trim(0);
        added = residentBytes() - residentBefore;
        peakDuring = peakResident();
        // With terrain: save and open.
        timer.restart();
        saveDocument(doc, with);
        saveWith.push_back(double(timer.nsecsElapsed()) / 1e6);
        timer.restart();
        auto reopened = loadDocument(with);
        openWith.push_back(double(timer.nsecsElapsed()) / 1e6);
        check(reopened.bodies().size() == doc.bodies().size(), "the terrain reopens");
        withoutBytes = QFileInfo(without).size();
        withBytes = QFileInfo(with).size();
        last = std::move(doc);
    }
    auto best = [](const std::vector<double> &v) { return *std::min_element(v.begin(), v.end()); };
    QJsonObject json{
        {"source", name},
        {"sourceBytes", double(bytes.size())},
        {"triangles", triangles},
        {"tiles", tiles},
        {"runs", repeat},
        {"parseMs", best(parse)},
        {"prepareAndTriangulateMs", best(triangulate)},
        {"dryRunMs", best(dryRun)},
        {"dryRunMedianMs", median(dryRun)},
        {"commitMs", best(commit)},
        {"commitMedianMs", median(commit)},
        {"saveWithoutMs", best(saveWithout)},
        {"saveWithMs", best(saveWith)},
        {"saveAddedMs", best(saveWith) - best(saveWithout)},
        {"openWithoutMs", best(openWithout)},
        {"openWithMs", best(openWith)},
        {"openAddedMs", best(openWith) - best(openWithout)},
        {"addedResidentMiB", mib(added)},
        {"peakResidentMiB", mib(peakDuring)},
        {"fileWithoutBytes", double(withoutBytes)},
        {"fileWithBytes", double(withBytes)},
        {"fileGrowthMiB", mib(withBytes - withoutBytes)},
        {"containerWith", containerBreakdown(with)},
    };
    std::cerr << QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return {json, std::move(last)};
}

// D-044: cancel latency at the triangle limit.
QJsonObject cancelLatency(const char *name, const QByteArray &bytes, const QJsonObject &command,
                          int trials) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    // The reference: without and with a TerrainProgress, the same mesh; the run's length.
    auto plainRequest = parseTerrainSourceRequest(bytes, command);
    const auto plain =
        runTerrainImport(plainRequest.set, plainRequest.options, std::nullopt, plainRequest.provenance);
    check(plain.ok, std::string(name) + " imports");
    std::vector<std::pair<int, double>> reports;
    TerrainProgress observed({}, [&](TerrainStage stage, double fraction) {
        reports.emplace_back(int(stage), fraction);
    });
    const auto start = Clock::now();
    auto request = parseTerrainSourceRequest(bytes, command, true, &observed);
    const auto reported =
        runTerrainImport(request.set, request.options, std::nullopt, request.provenance, &observed);
    const double runMs = ms(Clock::now() - start);
    check(reported.ok && reported.mesh.vertices == plain.mesh.vertices &&
              reported.mesh.triangles == plain.mesh.triangles && reported.record == plain.record &&
              reported.report == plain.report,
          std::string(name) + ": progress does not change the result");
    for (std::size_t i = 1; i < reports.size(); ++i)
        check(reports[i] > reports[i - 1], std::string(name) + ": progress rises");
    check(!reports.empty() && reports.back() == std::pair<int, double>{int(TerrainStage::Tile), 1.0},
          std::string(name) + ": progress reaches the end");
    request = {};
    std::vector<double> latencies;
    std::vector<double> worstByStage(terrainStageCount, 0);
    int cancelled = 0;
    for (int trial = 0; trial < trials; ++trial) {
        const double at = runMs * (trial + 0.5) / trials;
        std::atomic<bool> flag{false};
        std::atomic<int> stage{-1};
        Clock::time_point requested;
        TerrainProgress progress([&] { return flag.load(std::memory_order_relaxed); },
                                 [&](TerrainStage s, double) { stage.store(int(s)); });
        const auto begin = Clock::now();
        std::thread canceller([&] {
            std::this_thread::sleep_until(begin + std::chrono::microseconds(qint64(at * 1000)));
            requested = Clock::now();
            flag.store(true);
        });
        bool stopped = false;
        try {
            auto cancelledRequest = parseTerrainSourceRequest(bytes, command, true, &progress);
            const auto result = runTerrainImport(cancelledRequest.set, cancelledRequest.options,
                                                 std::nullopt, cancelledRequest.provenance,
                                                 &progress);
            stopped = result.cancelled;
            check(result.cancelled || result.ok, "a run finishes or is cancelled");
            check(!result.cancelled ||
                      (result.mesh.triangles.empty() && result.report.isEmpty()),
                  "a cancelled run keeps nothing");
        } catch (const TerrainCancelled &) {
            stopped = true;
        }
        const auto end = Clock::now();
        canceller.join();
        if (!stopped)
            continue; // finished before the request (the last trials of a fast run)
        ++cancelled;
        const double latency = ms(end - requested);
        latencies.push_back(latency);
        const int s = std::max(stage.load(), 0);
        worstByStage[std::size_t(s)] = std::max(worstByStage[std::size_t(s)], latency);
    }
    check(!latencies.empty(), "some runs were cancelled");
    QJsonObject byStage;
    for (int s = 0; s < terrainStageCount; ++s)
        byStage[terrainStageName(TerrainStage(s))] = worstByStage[std::size_t(s)];
    QJsonObject json{{"source", name},
                     {"triangles", double(plain.mesh.triangles.size())},
                     {"dryRunWithProgressMs", runMs},
                     {"progressReports", double(reports.size())},
                     {"trials", trials},
                     {"cancelled", cancelled},
                     {"latencyMaxMs", *std::max_element(latencies.begin(), latencies.end())},
                     {"latencyP95Ms", p95(latencies)},
                     {"latencyMedianMs", median(latencies)},
                     {"latencyMaxByStageMs", byStage}};
    std::cerr << QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return json;
}

class TimedViewport final : public Viewport {
  public:
    using Viewport::Viewport;
    double completeFrameMs() const { return completeFrameMs_; }

  protected:
    void paintGL() override {
        QElapsedTimer timer;
        timer.start();
        Viewport::paintGL();
        context()->functions()->glFinish();
        completeFrameMs_ = double(timer.nsecsElapsed()) / 1e6;
    }

  private:
    double completeFrameMs_{};
};

QJsonObject frames(Document &doc) {
    Id terrain{};
    for (const auto &[id, body] : doc.bodies())
        if (body->terrain)
            terrain = id;
    check(terrain != 0, "the frame document has a terrain");
    QWidget host;
    host.setWindowTitle("SketchyUp terrain benchmark");
    host.resize(1920, 1080);
    TimedViewport view(doc, &host);
    view.resize(1920, 1080);
    view.setSynchronousFrameTiming(true);
    host.show();
    check(QTest::qWaitForWindowExposed(&host), "viewport exposed");
    bool sized{};
    for (int attempt = 0; attempt < 100 && !sized; ++attempt) {
        view.setFixedSize(qRound(1920 / view.devicePixelRatioF()),
                          qRound(1080 / view.devicePixelRatioF()));
        QTest::qWait(25);
        sized = view.grabFramebuffer().size() == QSize(1920, 1080);
    }
    check(sized, "a 1920 x 1080 framebuffer");
    view.fit();
    QCoreApplication::processEvents();
    auto run = [&](const char *name) {
        auto frame = [&] {
            QCoreApplication::processEvents();
            const auto before = view.renderStats().frames;
            const auto image = view.grabFramebuffer();
            check(!image.isNull() && view.renderStats().frames > before, "a fresh frame");
        };
        for (int i = 0; i < 10; ++i)
            frame();
        std::vector<double> complete, scene;
        for (int i = 0; i < 50; ++i) {
            frame();
            complete.push_back(view.completeFrameMs());
            scene.push_back(view.lastFrameMs());
        }
        return QJsonObject{{"display", name},
                           {"frames", 50},
                           {"completeFrameP95Ms", p95(complete)},
                           {"completeFrameMedianMs", median(complete)},
                           {"sceneP95Ms", p95(scene)}};
    };
    QJsonArray results;
    results.append(run("default"));
    TerrainDisplay display;
    display.contours = true;
    display.contourInterval = 1;
    display.shading = TerrainShading::ElevationBands;
    setTerrainDisplay(doc, terrain, display);
    view.update();
    QCoreApplication::processEvents();
    results.append(run("contours 1 m and elevation bands"));
    const auto *renderer = reinterpret_cast<const char *>(
        view.context()->functions()->glGetString(GL_RENDERER));
    return {{"platform", QGuiApplication::platformName()},
            {"renderer", renderer ? renderer : ""},
            {"framebuffer", "1920x1080"},
            {"results", results}};
}
} // namespace

// ---- R096.f1: input event to presented frame for a sculpt preview and commit -----------------
// R096.f1b: --terrain-section selects the terrain and shows its Entity Info section, refreshed on
// every change as the window does, so its reads (the "changed since generated" chip) are timed in
// the release event.
bool withTerrainSection = false;
QJsonObject sculptFrames(const char *name, const QByteArray &bytes, const QJsonObject &command,
                         int strokes) {
    auto doc = baseDocument();
    const auto imported = importTerrainSource(doc, bytes, command);
    const Id terrain = imported["terrain"].toString().toULongLong();
    {
        auto display = doc.bodies().at(terrain)->terrain->display;
        display.contours = true;
        display.contourInterval = 1;
        setTerrainDisplay(doc, terrain, display);
    }
    QWidget host;
    host.setWindowTitle("SketchyUp terrain sculpt benchmark");
    host.resize(1920, 1080);
    TimedViewport view(doc, &host);
    view.resize(1920, 1080);
    view.setSynchronousFrameTiming(true);
    host.show();
    check(QTest::qWaitForWindowExposed(&host), "viewport exposed");
    bool sized{};
    for (int attempt = 0; attempt < 100 && !sized; ++attempt) {
        view.setFixedSize(qRound(1920 / view.devicePixelRatioF()),
                          qRound(1080 / view.devicePixelRatioF()));
        QTest::qWait(25);
        sized = view.grabFramebuffer().size() == QSize(1920, 1080);
    }
    check(sized, "a 1920 x 1080 framebuffer");
    view.standardView(1); // top: the whole terrain in view
    view.fit();
    view.setTool(Viewport::Tool::Sculpt);
    QCoreApplication::processEvents();
    std::unique_ptr<TerrainSection> section;
    if (withTerrainSection) {
        view.setSelection(terrain);
        section = std::make_unique<TerrainSection>(doc, view);
        QObject::connect(&view, &Viewport::changed, section.get(), &TerrainSection::refresh);
        section->show();
        section->refresh();
        QCoreApplication::processEvents();
        check(section->isVisible() && section->terrain() == terrain,
              "the Terrain section shows the selected terrain");
    }
    auto frame = [&] {
        const auto before = view.renderStats().frames;
        const auto image = view.grabFramebuffer();
        check(!image.isNull() && view.renderStats().frames > before, "a fresh frame");
    };
    for (int i = 0; i < 3; ++i)
        frame(); // cold display build (surfaces, contours, tile meshes)
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    auto send = [&](QEvent::Type type, QPointF point, Qt::MouseButton button,
                    Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, view.mapToGlobal(point), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &event);
    };
    const auto world = doc.worldTransform(terrain);
    // The largest tiles, with their plan centre and smaller side (model coordinates).
    struct Tile {
        std::size_t triangles;
        Vec3 centre;
        double side;
        Vec3 low, high;
    };
    std::vector<Tile> tiles;
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &body = *doc.bodies().at(id);
        Vec3 low{1e300, 1e300, 0}, high{-1e300, -1e300, 0};
        for (const auto &[vertex, p] : body.surface.vertices) {
            const auto m = world.point(p);
            low = {std::min(low.x, m.x), std::min(low.y, m.y), 0};
            high = {std::max(high.x, m.x), std::max(high.y, m.y), 0};
        }
        tiles.push_back({body.surface.faces.size(), (low + high) * 0.5,
                         std::min(high.x - low.x, high.y - low.y), low, high});
    }
    std::stable_sort(tiles.begin(), tiles.end(),
                     [](const Tile &a, const Tile &b) { return a.triangles > b.triangles; });
    const auto cell = doc.bodies().at(terrain)->terrain->tiling.cell;
    struct Samples {
        std::vector<double> preview, commit, previewPaint, commitPaint;
        std::vector<double> previewEvent, previewBuild, commitEvent, commitRebuild, baseline;
        std::vector<double> commitCommand, commitRefresh, commitViews;
        std::vector<std::size_t> triangles, vertices, tilesTouched;
        std::size_t excluded{}, digestsInEvent{};
    };
    auto stroke = [&](const Tile &tile, double radius, Samples &out, bool budget) {
        TerrainToolOptions options;
        options.radius = radius;
        options.amount = 0.5;
        view.setTerrainOptions(options);
        const auto z = [&](Vec3 p) {
            const auto found = terrainElevationAt(doc, terrain, {{p.x, p.y}});
            return Vec3{p.x, p.y, found.empty() ? 0 : found.front().elevation};
        };
        // The view frames the stroke's neighbourhood (16 radii across), so each pointer step is
        // many pixels, as when sculpting; the whole terrain stays loaded and drawn where visible.
        {
            const auto ground = z(tile.centre);
            view.frameBounds(ground - Vec3{8 * radius, 8 * radius, 1},
                             ground + Vec3{8 * radius, 8 * radius, 1});
        }
        frame();
        // Samples 0.3 radius apart (the stroke takes one every quarter radius) through the
        // middle, so a one-tile brush stays 0.36 of the tile's side from its middle.
        const auto start = z(tile.centre + Vec3{-radius * 0.45, 0.01, 0});
        send(QEvent::MouseMove, view.project(start), Qt::NoButton, Qt::NoButton);
        frame();
        const double baseline = view.completeFrameMs(); // a frame without a candidate
        send(QEvent::MouseButtonPress, view.project(start), Qt::LeftButton, Qt::LeftButton);
        frame();
        std::vector<double> preview, paint, events, builds;
        for (int step = 1; step <= 3; ++step) {
            const auto p = z(tile.centre + Vec3{-radius * 0.45 + radius * 0.3 * step, 0.01, 0});
            const auto t0 = Clock::now();
            send(QEvent::MouseMove, view.project(p), Qt::NoButton, Qt::LeftButton);
            events.push_back(ms(Clock::now() - t0));
            frame();
            preview.push_back(ms(Clock::now() - t0));
            paint.push_back(view.completeFrameMs());
            builds.push_back(view.renderStats().terrainPreviewMs);
        }
        const auto candidate = view.terrainPreviewShown();
        if (!candidate || !candidate->edit) {
            const auto brush = view.terrainBrush();
            std::cerr << "no candidate: terrain " << brush.terrain << " stroking "
                      << brush.stroking << " vertices " << brush.vertices << " refusal "
                      << brush.refusal.toStdString() << " radius " << radius << '\n';
            ++out.excluded;
            send(QEvent::MouseButtonRelease, view.project(z(tile.centre)), Qt::LeftButton,
                 Qt::NoButton);
            view.cancel();
            view.setTool(Viewport::Tool::Sculpt);
            return;
        }
        std::size_t triangles = 0;
        for (const auto &content : candidate->edit->region.tiles)
            triangles += content->triangles.size();
        const auto revision = doc.revision();
        const auto digests = TerrainDigest::computations();
        const auto t0 = Clock::now();
        send(QEvent::MouseButtonRelease,
             view.project(z(tile.centre + Vec3{radius * 0.45, 0.01, 0})), Qt::LeftButton,
             Qt::NoButton);
        const double commitEvent = ms(Clock::now() - t0);
        // The digest is computed by the worker; a frame must not wait for it (a race with the
        // worker can count it here, which is fine: the frame did not compute it).
        out.digestsInEvent += TerrainDigest::computations() - digests;
        frame();
        const double commit = ms(Clock::now() - t0);
        check(doc.revision() == revision + 1, "the stroke committed");
        if (budget && triangles > 2048)
            ++out.excluded;
        else {
            out.preview.insert(out.preview.end(), preview.begin(), preview.end());
            out.previewPaint.insert(out.previewPaint.end(), paint.begin(), paint.end());
            out.previewEvent.insert(out.previewEvent.end(), events.begin(), events.end());
            out.previewBuild.insert(out.previewBuild.end(), builds.begin(), builds.end());
            out.commit.push_back(commit);
            out.commitPaint.push_back(view.completeFrameMs());
            out.commitEvent.push_back(commitEvent);
            out.commitRebuild.push_back(view.renderStats().rebuildMs);
            out.commitCommand.push_back(view.renderStats().terrainCommandMs);
            out.commitRefresh.push_back(view.renderStats().terrainRefreshMs);
            out.commitViews.push_back(view.renderStats().terrainViewsMs);
            out.baseline.push_back(baseline);
            out.triangles.push_back(triangles);
            out.vertices.push_back(candidate->moving.size());
            out.tilesTouched.push_back(candidate->tilesTouched());
        }
        doc.undo();
        view.refresh();
        frame();
    };
    auto summary = [&](const Samples &s) {
        auto maxOf = [](const auto &v) { return v.empty() ? 0.0 : double(*std::max_element(v.begin(), v.end())); };
        QJsonObject json{{"strokes", double(s.commit.size())},
                         {"digestsComputedDuringCommitEvents", double(s.digestsInEvent)},
                         {"excluded", double(s.excluded)}};
        if (s.commit.empty())
            return json;
        json["previewP50Ms"] = median(s.preview);
        json["previewP95Ms"] = p95(s.preview);
        json["previewMaxMs"] = maxOf(s.preview);
        json["previewFrameP95Ms"] = p95(s.previewPaint);
        json["commitP50Ms"] = median(s.commit);
        json["commitP95Ms"] = p95(s.commit);
        json["commitMaxMs"] = maxOf(s.commit);
        json["commitFrameP95Ms"] = p95(s.commitPaint);
        // The parts: the event (pick and plan; for the commit, terrain.sculpt and the edit), the
        // preview display build and upload inside the frame, the cache rebuild inside the commit
        // frame, and a frame of the same view without a candidate.
        json["previewEventP95Ms"] = p95(s.previewEvent);
        json["previewBuildP95Ms"] = p95(s.previewBuild);
        json["commitEventP95Ms"] = p95(s.commitEvent);
        json["commitRebuildP95Ms"] = p95(s.commitRebuild);
        json["commitCommandP95Ms"] = p95(s.commitCommand);
        json["commitRefreshP95Ms"] = p95(s.commitRefresh);
        json["commitTerrainViewsP95Ms"] = p95(s.commitViews);
        json["previewCpuP95Ms"] = p95([&] {
            std::vector<double> cpu;
            for (std::size_t i = 0; i < s.previewEvent.size(); ++i)
                cpu.push_back(s.previewEvent[i] + s.previewBuild[i]);
            return cpu;
        }());
        json["commitCpuP95Ms"] = p95([&] {
            std::vector<double> cpu;
            for (std::size_t i = 0; i < s.commitEvent.size(); ++i)
                cpu.push_back(s.commitEvent[i] + s.commitRebuild[i]);
            return cpu;
        }());
        json["baselineFrameP95Ms"] = p95(s.baseline);
        json["baselineFrameP50Ms"] = median(s.baseline);
        std::vector<double> triangles(s.triangles.begin(), s.triangles.end()),
            vertices(s.vertices.begin(), s.vertices.end()),
            touched(s.tilesTouched.begin(), s.tilesTouched.end());
        json["touchedTrianglesP95"] = p95(triangles);
        json["verticesP95"] = p95(vertices);
        json["tilesTouchedMax"] = maxOf(touched);
        return json;
    };
    Samples one, nine;
    for (int i = 0; i < strokes && i < int(tiles.size()); ++i)
        stroke(tiles[std::size_t(i)], std::max(0.5, tiles[std::size_t(i)].side / 4), one, true);
    for (int i = 0; i < std::min(strokes, 16) && i < int(tiles.size()); ++i)
        stroke(tiles[std::size_t(i)], 1.1 * cell, nine, false);
    const auto *renderer = reinterpret_cast<const char *>(
        view.context()->functions()->glGetString(GL_RENDERER));
    return {{"source", name},
            {"triangles", double(doc.materializedTotals().terrainTriangles)},
            {"tiles", double(tiles.size())},
            {"renderer", renderer ? renderer : ""},
            {"platform", QGuiApplication::platformName()},
            {"framebuffer", "1920x1080"},
            {"oneTile", summary(one)},
            {"nineTiles", summary(nine)}};
}

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    bool frame = false;
    int repeat = 3, cancelTrials = 0, sculptStrokes = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frame")
            frame = true;
        else if (arg == "--sculpt-frames") {
            frame = true;
            sculptStrokes = 64;
            if (i + 1 < argc && std::atoi(argv[i + 1]) > 0)
                sculptStrokes = std::atoi(argv[++i]);
        }
        else if (arg == "--cancel") {
            cancelTrials = 100;
            if (i + 1 < argc && std::atoi(argv[i + 1]) > 0)
                cancelTrials = std::atoi(argv[++i]);
        }
        else if (arg == "--terrain-section")
            withTerrainSection = true;
        else if (arg == "--repeat" && i + 1 < argc)
            repeat = std::max(1, std::atoi(argv[++i]));
    }
    if (!frame)
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    try {
        const QJsonObject pointsCommand{{"format", "points"},
                                        {"units", "m"},
                                        {"columns", "XYZ"},
                                        {"delimiter", "whitespace"},
                                        {"header", false}};
        const QJsonObject contoursCommand{{"format", "dxf"}, {"units", "header"}};
        if (sculptStrokes) {
            QJsonArray sources;
            sources.append(sculptFrames("points", pointSource(125000), pointsCommand, sculptStrokes));
            sources.append(
                sculptFrames("contours", contourSource(416, 300), contoursCommand, sculptStrokes));
            QJsonObject out{{"benchmark", "terrain_budget_benchmark --sculpt-frames"},
                            {"terrainSection", withTerrainSection},
#ifdef SKETCHYUP_BENCHMARK_BUILD_TYPE
                            {"buildType", SKETCHYUP_BENCHMARK_BUILD_TYPE},
#endif
                            {"cpus", int(::sysconf(_SC_NPROCESSORS_ONLN))},
                            {"sources", sources}};
            std::cout << QJsonDocument(out).toJson().toStdString();
            return 0;
        }
        if (cancelTrials) {
            QJsonArray sources;
            sources.append(cancelLatency("points", pointSource(125000), pointsCommand, cancelTrials));
            sources.append(
                cancelLatency("contours", contourSource(416, 300), contoursCommand, cancelTrials));
            QJsonObject out{{"benchmark", "terrain_budget_benchmark --cancel"},
#ifdef SKETCHYUP_BENCHMARK_BUILD_TYPE
                            {"buildType", SKETCHYUP_BENCHMARK_BUILD_TYPE},
#endif
                            {"cpus", int(::sysconf(_SC_NPROCESSORS_ONLN))},
                            {"sources", sources}};
            std::cout << QJsonDocument(out).toJson().toStdString();
            return 0;
        }
        QTemporaryDir files;
        check(files.isValid(), "temporary directory");
        QJsonArray sources;
        auto points = measure("points", pointSource(125000),
                              {{"format", "points"},
                               {"units", "m"},
                               {"columns", "XYZ"},
                               {"delimiter", "whitespace"},
                               {"header", false}},
                              repeat, files);
        sources.append(points.json);
        sources.append(measure("contours", contourSource(416, 300),
                               {{"format", "dxf"}, {"units", "header"}}, repeat, files)
                           .json);
        QJsonObject out{{"benchmark", "terrain_budget_benchmark"},
#ifdef SKETCHYUP_BENCHMARK_BUILD_TYPE
                        {"buildType", SKETCHYUP_BENCHMARK_BUILD_TYPE},
#endif
                        {"cpus", int(::sysconf(_SC_NPROCESSORS_ONLN))},
                        {"sources", sources}};
        if (frame)
            out["frame"] = frames(*points.document);
        std::cout << QJsonDocument(out).toJson().toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
