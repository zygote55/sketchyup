#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "io/document_io.hpp"
#include "io/model_style_io.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
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
class TimedViewport final : public Viewport {
  public:
    using Viewport::Viewport;
    double completeFrameMs() const { return completeFrameMs_; }
    void beginEditFrame(const QElapsedTimer &started) {
        check(!editFramePending_, "Previous edit frame was completed");
        editStarted_ = started;
        editFrameMs_ = 0;
        editFramePending_ = true;
    }
    double editFrameMs() const {
        check(!editFramePending_ && editFrameMs_ > 0, "Edit reached a complete viewport frame");
        return editFrameMs_;
    }

  protected:
    void paintGL() override {
        QElapsedTimer timer;
        timer.start();
        Viewport::paintGL(); // Returns after QPainter finishes every viewport overlay.
        context()->functions()->glFinish();
        completeFrameMs_ = timer.nsecsElapsed() / 1e6;
        if (editFramePending_) {
            editFrameMs_ = editStarted_.nsecsElapsed() / 1e6;
            editFramePending_ = false; // Keep the first frame, before any readback repaint.
        }
    }

  private:
    double completeFrameMs_{};
    QElapsedTimer editStarted_;
    double editFrameMs_{};
    bool editFramePending_{};
};
AssetPayloadPtr largeTexture(int variant) {
    QFile file(variant == 0 ? ":/benchmark/front.png" : ":/benchmark/back.png");
    check(file.open(QIODevice::ReadOnly), "Open embedded maximum-size texture fixture");
    const auto bytes = file.readAll();
    const auto expected = variant == 0
                              ? "bef19ea75d4f1b1a636d6db100f1f0b32c89cc0f6f5301af159f8b27b31e940b"
                              : "dc7164dbfa82e0992f6d75fd5287e3fd17226fdbb30cb2142244d0c3a6b9c268";
    check(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == expected,
          "Frozen texture bytes agree across platforms");
    return std::make_shared<const AssetPayload>(
        std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
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
        check(argc <= 3, "Usage: real_model_benchmark [count] [repeated|unique|deep|far|textures]");
        bool ok = argc == 1;
        const int count = argc == 1 ? 1000 : QString::fromLocal8Bit(argv[1]).toInt(&ok);
        check(ok && count >= 1 && count <= 10000, "Instance count must be 1..10000");
        const QString scenario = argc == 3 ? QString::fromLocal8Bit(argv[2]) : "repeated";
        check(scenario == "repeated" || scenario == "unique" || scenario == "deep" ||
                  scenario == "far" || scenario == "textures",
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
            const auto body = prism(1);
            if (scenario == "textures") {
                const auto front = createAsset(doc, "4096px front", "image/png", largeTexture(0));
                const auto back = createAsset(doc, "4096px back", "image/png", largeTexture(1));
                assignMaterial(doc, body, {}, createMaterial(doc, "Front", {1, 1, 1}, 1, front),
                               true, false);
                assignMaterial(doc, body, {}, createMaterial(doc, "Back", {1, 1, 1}, 1, back),
                               false, true);
            }
            const auto component = createComponent(doc, body, "26-sided benchmark prism");
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
        {
            // Match ordinary load ownership: temporary parse/encoding buffers
            // do not remain resident alongside the measured viewport caches.
            auto json = QJsonDocument::fromJson(encodeDocument(doc)).object();
            json["documentId"] = "0000000000000000000000000000082a";
            doc = decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
        }
        size_t triangles{};
        for (const auto &[id, record] : doc.bodies()) {
            (void)id;
            triangles += record->surface.triangles().size();
        }
        check(triangles == size_t(count) * 100, "Fixture has exactly 100 triangles per instance");
        const auto fixtureHash =
            QCryptographicHash::hash(encodeContainer(doc), QCryptographicHash::Sha256).toHex();
        const auto fixtureMs = timer.nsecsElapsed() / 1e6;
        const auto fixtureRss = peakRss();
        QWidget host;
        host.setWindowTitle("SketchyUp performance benchmark");
        host.resize(1920, 1080);
        TimedViewport view(doc, &host);
        view.resize(1920, 1080);
        view.setSynchronousFrameTiming(true);
        QElapsedTimer textureReady;
        textureReady.start();
        if (qEnvironmentVariable("SKETCHYUP_BENCHMARK_FULLSCREEN") == "1")
            host.showFullScreen();
        else
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
        std::vector<double> frames, completeFrames, picks, edits, editFrames;
        auto frame = [&] {
            QCoreApplication::processEvents();
            const auto before = view.renderStats().frames;
            const auto image = view.grabFramebuffer();
            check(!image.isNull() && view.renderStats().frames > before, "Fresh benchmark frame");
            check(image.size() == QSize(1920, 1080),
                  "Benchmark requires an exact 1920 by 1080 framebuffer");
            return image.size();
        };
        double textureReadyMs{};
        if (scenario == "textures") {
            frame();
            while (view.texturesPending() && textureReady.elapsed() < 15000)
                QTest::qWait(10);
            frame(); // Consume the completed decoder generation and upload it.
            check(!view.texturesPending() && view.textureSummary().isEmpty(),
                  "Both maximum-size textures are ready without a color fallback");
            textureReadyMs = textureReady.nsecsElapsed() / 1e6;
        }
        for (int i = 0; i < 10; ++i)
            frame();
        const auto warmStats = view.renderStats();
        const auto warmMemory = view.geometryCacheMemory();
        QSize pixels;
        for (int i = 0; i < 50; ++i) {
            pixels = frame();
            frames.push_back(view.lastFrameMs());
            check(view.completeFrameMs() > 0 && view.completeFrameMs() >= view.lastFrameMs(),
                  "Complete viewport timing includes the measured GL scene");
            completeFrames.push_back(view.completeFrameMs());
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
        // Measure the production geometry index independently of worker queueing
        // and editor context policy. The actual viewport camera supplies pixels.
        InferenceIndex inference;
        timer.restart();
        inference.sync(doc);
        const auto inferenceBuildMs = timer.nsecsElapsed() / 1e6;
        const auto inferenceInitialBuilds = inference.bodyBuilds();
        check(inferenceInitialBuilds == doc.bodies().size(), "Index all fixture bodies");
        check(inference.primitiveCount() > triangles, "Inference includes edges and vertices");
        std::vector<double> inferenceQueries, inferenceUpdates;
        size_t inferenceVisitedMax{}, inferencePairsMax{}, inferenceCandidatesMin = SIZE_MAX;
        size_t inferenceTruncatedQueries{};
        auto infer = [&](Id placement) {
            const auto point = view.project(doc.worldTransform(placement).point({0, 0, 1}));
            const auto hit = view.pick(point);
            check(hit.first != 0, "Inference probe intersects rendered geometry");
            const InferenceQuery query{view.inferenceCamera(), point.x(), point.y(), 8};
            timer.restart();
            const auto result = inference.query(query);
            const auto elapsed = timer.nsecsElapsed() / 1e6;
            if (result.candidates.empty() || result.candidates.size() > 32) {
                QJsonArray clip, inverse;
                for (auto value : query.camera.clipFromWorld)
                    clip.append(value);
                for (auto value : query.camera.worldFromClip)
                    inverse.append(value);
                const QJsonObject diagnostic{{"scenario", scenario},
                                             {"placement", qint64(placement)},
                                             {"x", point.x()},
                                             {"y", point.y()},
                                             {"width", query.camera.width},
                                             {"height", query.camera.height},
                                             {"clipFromWorld", clip},
                                             {"worldFromClip", inverse},
                                             {"candidateCount", qint64(result.candidates.size())},
                                             {"truncated", result.truncated},
                                             {"visited", qint64(result.visitedPrimitives)}};
                std::cerr << QJsonDocument(diagnostic).toJson(QJsonDocument::Compact).toStdString()
                          << '\n';
                throw std::runtime_error("Inference probe returns a bounded candidate list");
            }
            inferenceTruncatedQueries += result.truncated;
            check(std::any_of(result.candidates.begin(), result.candidates.end(),
                              [&](const auto &candidate) { return candidate.body == hit.first; }),
                  "Inference includes the geometry hit by the actual viewport");
            inferenceVisitedMax = std::max(inferenceVisitedMax, result.visitedPrimitives);
            inferencePairsMax = std::max(inferencePairsMax, result.intersectionPairs);
            inferenceCandidatesMin = std::min(inferenceCandidatesMin, result.candidates.size());
            return elapsed;
        };
        for (int i = 0; i < 10; ++i)
            infer(instances[size_t(i) * (count - 1) / 9]);
        for (int i = 0; i < 50; ++i)
            inferenceQueries.push_back(infer(instances[size_t(i) * (count - 1) / 49]));
        inference.sync(doc);
        check(inference.bodyBuilds() == inferenceInitialBuilds,
              "Stationary inference queries and sync reuse every body cache");
        const auto editing = instances.front();
        const auto base = doc.bodies().at(editing)->transform;
        const auto baselineBytes = doc.historyBytes();
        for (int i = 0; i < 100; ++i) {
            timer.restart();
            view.beginEditFrame(timer);
            doc.transform(editing, Transform::translation({0, 0, .01 * (i + 1)}) * base);
            view.refresh();
            frame();
            edits.push_back(timer.nsecsElapsed() / 1e6);
            editFrames.push_back(view.editFrameMs());
            check(editFrames.back() <= edits.back(),
                  "First completed edit frame precedes the enclosing readback completion");
            const auto before = inference.bodyBuilds();
            timer.restart();
            inference.sync(doc);
            inferenceUpdates.push_back(timer.nsecsElapsed() / 1e6);
            check(inference.bodyBuilds() - before == (scenario == "unique" ? 1 : 2),
                  "Inference transform rebuilds only the changed placement");
        }
        const auto historyBytes = doc.historyBytes();
        for (int i = 0; i < 100; ++i)
            doc.undo();
        for (int i = 0; i < 100; ++i)
            doc.redo();
        check(doc.historyBytes() == historyBytes, "Undo/redo retains bounded history allocation");
        inference.sync(doc);
        frame();
        infer(editing); // The post-history index must describe the current revision.
        const auto finalMemory = view.geometryCacheMemory();
        check(finalMemory.bodyVectorCapacityBytes == warmMemory.bodyVectorCapacityBytes &&
                  finalMemory.bodyGpuPayloadBytes == warmMemory.bodyGpuPayloadBytes,
              "Repeated transform edits retain fixed body-cache capacity");
        check(view.renderStats().glError == 0, "Benchmark leaves no OpenGL error");
        check(!view.texturesPending() && view.textureSummary().isEmpty(),
              "Benchmark finishes without missing or over-budget textures");
        QJsonObject report{
            {"fixtureVersion", scenario == "repeated" ? 1 : 2},
            {"fixtureScenario", scenario},
            {"fixtureSha256", QString::fromLatin1(fixtureHash)},
            {"instances", qint64(doc.instances().size())},
            {"placements", count},
            {"outerGroups", outerGroups},
            {"coordinateOffset", QJsonArray{offset.x, offset.y, offset.z}},
            {"fixtureTextureCount", scenario == "textures" ? 2 : 0},
            {"fixtureTextureWidth", scenario == "textures" ? 4096 : 0},
            {"fixtureTextureHeight", scenario == "textures" ? 4096 : 0},
            {"fixtureTextureRgbaBytes", scenario == "textures" ? qint64(128 * 1024 * 1024) : 0},
            {"textureReadyAfterShowMs", textureReadyMs},
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
            {"hostFullScreen", host.isFullScreen()},
            {"viewportClippedByHost", view.width() > host.width() || view.height() > host.height()},
            {"scale", view.devicePixelRatioF()},
            {"style", encodeModelStyle(doc.style())},
            {"frameSamples", 50},
            {"warmupFrames", 10},
            {"gpuCompleteFrameP95Ms", p95(frames)},
            {"viewportGpuCompleteP95Ms", p95(completeFrames)},
            {"viewportTimingScope", "Complete Viewport::paintGL including QPainter overlays, "
                                    "followed by glFinish; excludes child widgets, framebuffer "
                                    "readback and compositor presentation. Scene timing retains "
                                    "its separate intermediate glFinish."},
            {"fixtureBufferScope", "Temporary JSON and canonical byte buffers are released before "
                                   "viewport construction; fixture RSS still includes their peak."},
            {"pickP95Ms", p95(picks)},
            {"editAndReadbackP95Ms", p95(edits)},
            {"editToViewportGpuCompleteP95Ms", p95(editFrames)},
            {"editFrameSamples", qint64(editFrames.size())},
            {"editFrameTimingScope",
             "From synchronous document mutation through the first "
             "complete Viewport::paintGL and glFinish; includes refresh "
             "and queued events before that frame, excludes subsequent "
             "readback/repaint, input queueing and compositor presentation. "
             "The enclosing editAndReadback measurement is retained."},
            {"inferenceInitialBuildMs", inferenceBuildMs},
            {"inferenceQueryP95Ms", p95(inferenceQueries)},
            {"inferenceUpdateP95Ms", p95(inferenceUpdates)},
            {"inferenceQuerySamples", 50},
            {"inferenceWarmupQueries", 10},
            {"inferenceUpdateSamples", 100},
            {"inferencePrimitiveCount", qint64(inference.primitiveCount())},
            {"inferenceInitialBodyBuilds", qint64(inferenceInitialBuilds)},
            {"inferenceFinalBodyBuilds", qint64(inference.bodyBuilds())},
            {"inferenceVisitedPrimitivesMax", qint64(inferenceVisitedMax)},
            {"inferenceIntersectionPairsMax", qint64(inferencePairsMax)},
            {"inferenceCandidatesMin", qint64(inferenceCandidatesMin)},
            {"inferenceValidatedQueries", 61},
            {"inferenceTruncatedQueries", qint64(inferenceTruncatedQueries)},
            {"inferenceTimingScope",
             "Production geometry index, actual viewport camera, 8 logical "
             "pixel radius, all editing contexts; excludes worker queueing, "
             "editor eligibility policy and displayed feedback. Updates "
             "are timed separately after edit/readback."},
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
