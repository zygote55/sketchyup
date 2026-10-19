// R095.f (ADR 0168 section 6, "Budgets"): measures the 250,000-triangle terrain budgets.
//
//   terrain_budget_benchmark [--repeat N] [--frame]
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
#include "app/surface_format.hpp"
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
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <iostream>
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
    int repeat = 3;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frame")
            frame = true;
        else if (arg == "--repeat" && i + 1 < argc)
            repeat = std::max(1, std::atoi(argv[++i]));
    }
    if (!frame)
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    try {
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
