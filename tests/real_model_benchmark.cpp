#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include "io/model_style_io.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <numbers>
#include <sys/resource.h>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
double p95(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples.at(size_t(std::ceil(samples.size() * .95)) - 1);
}
qint64 peakRss() {
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "Read benchmark RSS");
    return qint64(usage.ru_maxrss) * 1024;
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
    QApplication app(argc, argv);
    try {
        check(argc <= 3, "Usage: real_model_benchmark [count] [repeated|unique|deep|far]");
        bool ok = argc == 1;
        const int count = argc == 1 ? 1000 : QString::fromLocal8Bit(argv[1]).toInt(&ok);
        check(ok && count >= 1 && count <= 10000, "Instance count must be 1..10000");
        const QString scenario = argc == 3 ? QString::fromLocal8Bit(argv[2]) : "repeated";
        check(scenario == "repeated" || scenario == "unique" || scenario == "deep" ||
                  scenario == "far",
              "Unknown benchmark scenario");
        QElapsedTimer timer;
        timer.start();
        Document doc;
        auto prism = [&](double radius) {
            std::vector<Vec3> loop;
            for (int i = 0; i < 26; ++i) {
                const auto angle = 2 * std::numbers::pi * i / 26;
                loop.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
            }
            const auto body = doc.addFace({loop});
            doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
            return body;
        };
        const int columns = int(std::ceil(std::sqrt(count)));
        std::vector<Id> instances;
        if (scenario == "unique") {
            // Distinct authoritative surfaces, not merely separate definitions
            // of identical geometry. The visible triangle count stays fixed.
            for (int i = 0; i < count; ++i) {
                const auto body = prism(1 + .2 * i / count);
                doc.transform(
                    body, Transform::translation({4.0 * (i % columns), 4.0 * (i / columns), 0}));
                instances.push_back(body);
            }
        } else {
            // Preserve the original repeated fixture, including allocation and
            // revision order, so its canonical hash remains comparable.
            const auto component = createComponent(doc, prism(1), "26-sided benchmark prism");
            instances.push_back(component.instance);
            for (int i = 1; i < count; ++i)
                instances.push_back(placeComponent(doc, component.definition,
                                                   Transform::translation({4.0 * (i % columns),
                                                                           4.0 * (i / columns), 0}))
                                        .instance);
        }
        const int outerGroups = scenario == "deep" ? 32 : scenario == "far" ? 1 : 0;
        const Vec3 offset = scenario == "far" ? Vec3{900000, -900000, 900000} : Vec3{};
        if (outerGroups) {
            auto group = createGroup(doc, std::set<Id>(instances.begin(), instances.end()),
                                     "Benchmark root");
            for (int i = 1; i < outerGroups; ++i)
                group = createGroup(doc, {group}, "Benchmark nesting " + std::to_string(i));
            if (scenario == "far")
                doc.transform(group, Transform::translation(offset));
        }
        auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
        json["documentId"] = "0000000000000000000000000000082a";
        doc = decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
        size_t triangles{};
        for (const auto &[id, record] : doc.bodies()) {
            (void)id;
            triangles += record->surface.triangles().size();
        }
        check(triangles == size_t(count) * 100, "Fixture has exactly 100 triangles per instance");
        const auto canonical = encodeContainer(doc);
        const auto fixtureHash =
            QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
        const auto fixtureMs = timer.nsecsElapsed() / 1e6;
        const auto fixtureRss = peakRss();
        QWidget host;
        host.setWindowTitle("SketchyUp performance benchmark");
        host.resize(1920, 1080);
        Viewport view(doc, &host);
        view.resize(1920, 1080);
        view.setSynchronousFrameTiming(true);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Benchmark viewport exposed");
        // Native Wayland may deliver the final output scale after first exposure.
        // Keep the child framebuffer independent of both tiling and that late scale.
        bool sized{};
        QSize observed;
        for (int attempt = 0; attempt < 100 && !sized; ++attempt) {
            view.setFixedSize(qRound(1920 / view.devicePixelRatioF()),
                              qRound(1080 / view.devicePixelRatioF()));
            QTest::qWait(25);
            observed = view.grabFramebuffer().size();
            sized = observed == QSize(1920, 1080);
        }
        if (!sized)
            throw std::runtime_error(QString("Benchmark framebuffer %1x%2, logical %3x%4, scale %5")
                                         .arg(observed.width())
                                         .arg(observed.height())
                                         .arg(view.width())
                                         .arg(view.height())
                                         .arg(view.devicePixelRatioF())
                                         .toStdString());
        view.fit();
        QCoreApplication::processEvents();
        std::vector<double> frames, picks, edits;
        auto frame = [&] {
            QCoreApplication::processEvents();
            const auto before = view.renderStats().frames;
            const auto image = view.grabFramebuffer();
            check(!image.isNull() && view.renderStats().frames > before, "Fresh benchmark frame");
            check(image.size() == QSize(1920, 1080),
                  "Benchmark requires an exact 1920 by 1080 framebuffer");
            return image.size();
        };
        for (int i = 0; i < 10; ++i)
            frame();
        const auto warmStats = view.renderStats();
        const auto warmMemory = view.geometryCacheMemory();
        QSize pixels;
        for (int i = 0; i < 50; ++i) {
            pixels = frame();
            frames.push_back(view.lastFrameMs());
            const auto instance = i % count;
            const auto point =
                view.project(doc.worldTransform(instances[instance]).point({0, 0, 1}));
            timer.restart();
            const auto hit = view.pick(point);
            picks.push_back(timer.nsecsElapsed() / 1e6);
            check(hit.first != 0, "Measured picking finds actual fixture geometry");
        }
        check(view.renderStats().bodyMeshBuilds == warmStats.bodyMeshBuilds &&
                  view.renderStats().geometryUploads == warmStats.geometryUploads,
              "Stationary frames reuse geometry caches");
        const auto editing = instances.front();
        const auto base = doc.bodies().at(editing)->transform;
        const auto baselineBytes = doc.historyBytes();
        for (int i = 0; i < 100; ++i) {
            timer.restart();
            doc.transform(editing, Transform::translation({0, 0, .01 * (i + 1)}) * base);
            view.refresh();
            frame();
            edits.push_back(timer.nsecsElapsed() / 1e6);
        }
        const auto historyBytes = doc.historyBytes();
        for (int i = 0; i < 100; ++i)
            doc.undo();
        for (int i = 0; i < 100; ++i)
            doc.redo();
        check(doc.historyBytes() == historyBytes, "Undo/redo retains bounded history allocation");
        const auto finalMemory = view.geometryCacheMemory();
        check(finalMemory.bodyVectorCapacityBytes == warmMemory.bodyVectorCapacityBytes &&
                  finalMemory.bodyGpuPayloadBytes == warmMemory.bodyGpuPayloadBytes,
              "Repeated transform edits retain fixed body-cache capacity");
        check(view.renderStats().glError == 0, "Benchmark leaves no OpenGL error");
        QJsonObject report{
            {"fixtureVersion", scenario == "repeated" ? 1 : 2},
            {"fixtureScenario", scenario},
            {"fixtureSha256", QString::fromLatin1(fixtureHash)},
            {"instances", qint64(doc.instances().size())},
            {"placements", count},
            {"outerGroups", outerGroups},
            {"coordinateOffset", QJsonArray{offset.x, offset.y, offset.z}},
            {"triangles", qint64(triangles)},
            {"bodies", qint64(doc.bodies().size())},
            {"platform", QGuiApplication::platformName()},
            {"graphics", view.graphicsDescription()},
            {"qtVersion", qVersion()},
            {"kernel", QSysInfo::kernelVersion()},
            {"buildType", SKETCHYUP_BENCHMARK_BUILD_TYPE},
            {"pixelWidth", pixels.width()},
            {"pixelHeight", pixels.height()},
            {"hostLogicalWidth", host.width()},
            {"hostLogicalHeight", host.height()},
            {"viewportClippedByHost", view.width() > host.width() || view.height() > host.height()},
            {"scale", view.devicePixelRatioF()},
            {"style", encodeModelStyle(doc.style())},
            {"frameSamples", 50},
            {"warmupFrames", 10},
            {"gpuCompleteFrameP95Ms", p95(frames)},
            {"pickP95Ms", p95(picks)},
            {"editAndReadbackP95Ms", p95(edits)},
            {"fixtureBuildMs", fixtureMs},
            {"fixturePeakRssBytes", fixtureRss},
            {"finalPeakRssBytes", peakRss()},
            {"initialHistoryBytes", qint64(baselineBytes)},
            {"retainedHistoryBytes", qint64(historyBytes)},
            {"bodyMeshBuilds", qint64(view.renderStats().bodyMeshBuilds)},
            {"uploadedBytesCumulative", qint64(view.renderStats().uploadedBytes)},
            {"bodyCacheVectorCapacityBytes", qint64(finalMemory.bodyVectorCapacityBytes)},
            {"bodyCacheGpuPayloadBytes", qint64(finalMemory.bodyGpuPayloadBytes)},
            {"cacheAccountingScope",
             "Body geometry vector capacity and GPU vertex payload only; excludes maps, textures, "
             "overlays, drivers and allocator overhead. RSS includes the process."},
            {"timingScope", "OpenGL scene through glFinish; excludes painter overlays and "
                            "compositor. Edits include framebuffer readback."},
            {"releaseAcceptance", false}};
        std::cout << QJsonDocument(report).toJson().toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
