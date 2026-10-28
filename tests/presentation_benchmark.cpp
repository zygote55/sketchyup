// The ADR 0173 §5 budget measurements (R099.e). Not built by default:
//   cmake --build build/release --target presentation_benchmark
//   build/release/presentation_benchmark <mode> [runs]
// Modes: plan (planCameraFrames, 32 keyframes), edit (create, update and erase of one path and one
// preset), load (decode of a document with 64 paths and 16 presets), capture (PresentationCapture on
// the quantities-b document, 250,000 terrain triangles, 32 keyframes), prepare (PreparedRender::prepare
// per frame of that capture) and probe (the codec probe: first, then cached). Each mode prints one
// JSON line with the runs, the p50, p95 and maximum in milliseconds, and its facts. The program
// asserts no budget; the record compares the numbers with ADR 0173 §5.
#include "core/camera_motion.hpp"
#include "core/presentation.hpp"
#include "core/presentation_records.hpp"
#include "core/render_settings.hpp"
#include "core/scenes.hpp"
#include "integrations/blender_job.hpp"
#include "integrations/presentation_batch.hpp"
#include "integrations/presentation_store.hpp"
#include "integrations/presentation_request.hpp"
#include "automation/inspection.hpp"
#include "io/document_io.hpp"
#include "report_fixtures.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

// Keeps the optimiser from removing a measured call.
std::size_t sink = 0;

struct Stats {
    std::size_t runs{};
    double p50{}, p95{}, max{};
};

double millis(std::chrono::steady_clock::duration span) {
    return std::chrono::duration<double, std::milli>(span).count();
}

Stats summarize(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    Stats stats;
    stats.runs = samples.size();
    const auto at = [&](double quantile) {
        const auto rank = static_cast<std::size_t>(std::ceil(quantile * samples.size()));
        return samples[std::max<std::size_t>(rank, 1) - 1];
    };
    stats.p50 = at(0.50);
    stats.p95 = at(0.95);
    stats.max = samples.back();
    return stats;
}

// Times `run` once per repetition and returns the samples in milliseconds.
std::vector<double> sample(int runs, const std::function<void()> &run) {
    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
        const auto start = std::chrono::steady_clock::now();
        run();
        samples.push_back(millis(std::chrono::steady_clock::now() - start));
    }
    return samples;
}

void print(const char *mode, const Stats &stats, const std::string &facts = {}) {
    std::printf("{\"mode\":\"%s\",\"runs\":%zu,\"p50_ms\":%.3f,\"p95_ms\":%.3f,\"max_ms\":%.3f%s}\n",
                mode, stats.runs, stats.p50, stats.p95, stats.max, facts.c_str());
    std::fflush(stdout);
}

// Thirty-two cameras that differ in yaw, pitch and distance, so the planned poses are not trivial.
std::vector<SceneCamera> cameras(std::size_t count) {
    std::vector<SceneCamera> result;
    for (std::size_t i = 0; i < count; ++i) {
        SceneCamera camera;
        camera.yaw = -180 + 11.0 * static_cast<double>(i);
        camera.pitch = i % 2 ? 20 : 40;
        camera.distance = 10 + static_cast<double>(i);
        result.push_back(camera);
    }
    return result;
}

// Saved scenes with cameras, one per camera, in presentation order.
std::vector<Id> addScenes(Document &doc, std::size_t count, const std::string &prefix) {
    std::vector<Id> ids;
    const auto poses = cameras(count);
    for (std::size_t i = 0; i < count; ++i) {
        SceneSnapshot view;
        view.camera = poses[i];
        ids.push_back(createScene(doc, prefix + std::to_string(i + 1), view));
    }
    return ids;
}

void planMode(int runs) {
    const auto poses = cameras(32);
    const CameraPathTiming timing{24, 7, 0}; // The largest 32-keyframe plan at or under 240 frames.
    const auto frames = planCameraFrames(poses, timing).size();
    const auto stats = summarize(sample(runs, [&] { sink += planCameraFrames(poses, timing).size(); }));
    print("plan", stats, ",\"keyframes\":32,\"framesPerSecond\":24,\"transitionSteps\":7,\"holdSteps\":0,"
                          "\"frames\":" + std::to_string(frames));
}

void editMode(int runs) {
    Document doc;
    const auto first = addScenes(doc, 1, "A")[0];
    const auto second = addScenes(doc, 1, "B")[0];
    const CameraPathTiming timing{24, 24, 12};
    // One path and one preset at a time, erased at the end of each cycle, so the counts stay at
    // one and the limits never bind.
    std::vector<double> create, update, erase, presetCreate, presetUpdate, presetErase;
    for (int i = 0; i < runs; ++i) {
        const auto name = "Path " + std::to_string(i);
        const auto start = std::chrono::steady_clock::now();
        const auto path = createCameraPath(doc, name, {first, second}, timing);
        create.push_back(millis(std::chrono::steady_clock::now() - start));
        const auto updateStart = std::chrono::steady_clock::now();
        updateCameraPath(doc, path, name + " edited", {second, first, second}, timing);
        update.push_back(millis(std::chrono::steady_clock::now() - updateStart));
        const auto eraseStart = std::chrono::steady_clock::now();
        eraseCameraPath(doc, path);
        erase.push_back(millis(std::chrono::steady_clock::now() - eraseStart));

        const auto presetStart = std::chrono::steady_clock::now();
        const auto preset = createRenderPreset(doc, "Preset " + std::to_string(i), RenderSettings{});
        presetCreate.push_back(millis(std::chrono::steady_clock::now() - presetStart));
        const auto presetUpdateStart = std::chrono::steady_clock::now();
        updateRenderPreset(doc, preset, "Preset " + std::to_string(i) + " edited", RenderSettings{});
        presetUpdate.push_back(millis(std::chrono::steady_clock::now() - presetUpdateStart));
        const auto presetEraseStart = std::chrono::steady_clock::now();
        eraseRenderPreset(doc, preset);
        presetErase.push_back(millis(std::chrono::steady_clock::now() - presetEraseStart));
    }
    print("edit-path-create", summarize(create));
    print("edit-path-update", summarize(update));
    print("edit-path-erase", summarize(erase));
    print("edit-preset-create", summarize(presetCreate));
    print("edit-preset-update", summarize(presetUpdate));
    print("edit-preset-erase", summarize(presetErase));
}

void loadMode(int runs) {
    Document bare;
    addScenes(bare, 2, "Scene ");
    Document full;
    const auto scenes = addScenes(full, 2, "Scene ");
    for (int i = 0; i < 64; ++i)
        createCameraPath(full, "Path " + std::to_string(i), {scenes[0], scenes[1]});
    for (int i = 0; i < 16; ++i)
        createRenderPreset(full, "Preset " + std::to_string(i), RenderSettings{});
    const auto bytes = encodeDocument(full);
    const auto bareBytes = encodeDocument(bare);
    const auto stats = summarize(sample(runs, [&] {
        sink += decodeDocument(bytes).cameraPaths().size();
    }));
    print("load", stats,
          ",\"paths\":64,\"presets\":16,\"documentBytes\":" + std::to_string(bytes.size()) +
              ",\"presentationTableBytes\":" + std::to_string(bytes.size() - bareBytes.size()));
}

// The quantities-b terrain document: a 249,218-triangle terrain (353 by 353 cells) and no placements. The
// render snapshot refuses more than 20,000 bodies, so the placements of quantities-b cannot be captured.
PresentationPlan tourPlan(const std::vector<Id> &keyframes) {
    PresentationPlan plan;
    plan.path = "Tour";
    plan.keyframes = keyframes;
    plan.timing = {24, 7, 0};
    RenderSettings final;
    final.width = 1024;
    final.height = 768;
    final.samples = 32;
    plan.presets = {{"Final", final}};
    return plan;
}

void captureMode(int runs) {
    auto doc = report_fixtures::buildQuantitiesB(0);
    const auto keyframes = addScenes(doc, 32, "Scene ");
    const auto plan = tourPlan(keyframes);
    const auto frames = PresentationCapture::capture(doc, plan).frames().size();
    const auto stats = summarize(sample(runs, [&] {
        sink += PresentationCapture::capture(doc, plan).frames().size();
    }));
    const auto encodeStats = summarize(sample(runs, [&] { sink += encodeDocument(doc).size(); }));
    print("capture", stats,
          ",\"frames\":" + std::to_string(frames) +
              ",\"document\":\"quantities-b terrain, 249218 triangles, no placements\"");
    print("capture-encode-only", encodeStats);
}

// 3,700 bodies, each one planar 27-gon face: 92,500 triangles and 99,900 vertices, the R099.b
// document. Each GLB export is bounded (256 MiB binary, 16 MiB JSON), and the JSON grows with the
// bodies, which is why 6,000 placements of the part, at 72,000 faces, were refused.
Document polygonBodies() {
    Document doc;
    for (int body = 0; body < 3700; ++body) {
        std::vector<Vec3> ring;
        for (int k = 0; k < 27; ++k) {
            const double angle = 2 * std::acos(-1.0) * k / 27;
            ring.push_back({3.0 * (body % 100) + std::cos(angle),
                            3.0 * (body / 100) + std::sin(angle), 0.0});
        }
        doc.addFace({ring});
    }
    return doc;
}

void prepareMode(int runs) {
    auto doc = polygonBodies();
    const auto keyframes = addScenes(doc, 32, "Scene ");
    const auto capture = PresentationCapture::capture(doc, tourPlan(keyframes));
    const std::size_t last = capture.frames().size() - 1;
    const std::size_t indices[] = {0, last / 2, last};
    std::vector<double> samples;
    for (int i = 0; i < runs; ++i) {
        const auto snapshot = capture.frame(0, indices[i % 3]);
        const auto start = std::chrono::steady_clock::now();
        sink += PreparedRender::prepare(snapshot) != nullptr;
        samples.push_back(millis(std::chrono::steady_clock::now() - start));
    }
    print("prepare", summarize(samples), ",\"frames\":3,\"indices\":\"first, middle, last\",\"document\":\"3700 bodies of one 27-gon face, 92500 triangles\"");
}

void probeMode(int runs) {
    const auto blender = QStandardPaths::findExecutable("blender");
    if (blender.isEmpty())
        throw std::runtime_error("blender is not on PATH");
    BlenderJob::Options options;
    options.executable = blender;
    PresentationVideoChecks checks;
    const auto first = sample(1, [&] { sink += checks.check(options).codecs.size(); });
    const auto answer = checks.check(options);
    print("probe-first", summarize(first),
          std::string(",\"checked\":") + (answer.checked ? "true" : "false") + ",\"codecs\":\"" +
              answer.codecs.join(",").toStdString() + "\"");
    print("probe-cached", summarize(sample(runs, [&] { sink += checks.check(options).codecs.size(); })));
    print("probe-cached-read", summarize(sample(runs, [&] {
        sink += checks.cached(options).has_value();
    })));
}

void describeMode(int runs) {
    // The CLI's own probe: the blocking form that src/automation/main.cpp installs.
    PresentationVideoChecks checks;
    installBlockingPresentationVideoProbe(checks);
    Document doc;
    const auto scenes = addScenes(doc, 2, "Scene ");
    const auto path = createCameraPath(doc, "Tour", {scenes[0], scenes[1]});
    const auto preset = createRenderPreset(doc, "Final", RenderSettings{});
    QJsonObject request{{"apiVersion", 1},
                        {"documentId", QString::fromStdString(doc.identity())},
                        {"expectedRevision", QString::number(doc.revision())},
                        {"query", "presentation.describe"},
                        {"path", QString::number(path)},
                        {"presets", QJsonArray{QString::number(preset)}},
                        {"video", true}};
    const auto run = [&] {
        const auto answer = inspectDocument(doc, request)["data"].toObject();
        sink += answer.size();
        return answer;
    };
    const auto first = sample(1, [&] { run(); });
    const auto answer = run();
    const auto blender = answer["blender"].toObject();
    print("describe-first", summarize(first),
          std::string(",\"videoChecked\":") +
              (blender["videoEncoders"].toObject()["checked"].toBool() ? "true" : "false"));
    print("describe-cached", summarize(sample(runs, [&] { run(); })));
}

// The scripted-run fixture (R099.e): the 3,700-body document saved as a native file with four saved
// scenes and four render presets, so the CLI can run a real batch on the document the per-frame
// row measures. Written to a new file only.
void writeModelMode(const QString &path) {
    auto doc = polygonBodies();
    for (int i = 0; i < 4; ++i) {
        SceneSnapshot view;
        view.camera = SceneCamera{};
        view.camera->target = {150, 55, 0};
        view.camera->distance = 300;
        view.camera->yaw = -60 + 40.0 * i;
        view.camera->pitch = 35;
        createScene(doc, "Scene " + std::to_string(i + 1), view);
    }
    createRenderPreset(doc, "Draft", RenderSettings{640, 480, 16, 0, RenderEngine::Cycles});
    createRenderPreset(doc, "Preview", RenderSettings{512, 384, 8, 0, RenderEngine::Cycles});
    for (int i = 1; i <= 4; ++i) // The §5 whole-batch settings: 1024 by 768 at 32 samples.
        createRenderPreset(doc, "Final " + std::to_string(i), RenderSettings{1024, 768, 32, 0, RenderEngine::Cycles});
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw std::runtime_error("The model file must be new");
    if (file.write(encodeDocument(doc)) <= 0)
        throw std::runtime_error("The model file could not be written");
    print("write-model", Stats{1, 0, 0, 0}, ",\"bodies\":3700,\"triangles\":92500,\"scenes\":4,\"presets\":6");
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    const auto mode = arguments.value(1);
    const int runs = arguments.size() > 2 ? arguments.value(2).toInt() : 0;
    try {
        if (mode == "plan")
            planMode(runs > 0 ? runs : 200);
        else if (mode == "edit")
            editMode(runs > 0 ? runs : 100);
        else if (mode == "load")
            loadMode(runs > 0 ? runs : 20);
        else if (mode == "capture")
            captureMode(runs > 0 ? runs : 20);
        else if (mode == "prepare")
            prepareMode(runs > 0 ? runs : 6);
        else if (mode == "probe")
            probeMode(runs > 0 ? runs : 5);
        else if (mode == "describe")
            describeMode(runs > 0 ? runs : 5);
        else if (mode == "write-model" && arguments.size() > 2)
            writeModelMode(arguments.value(2));
        else {
            std::fprintf(stderr, "usage: presentation_benchmark plan|edit|load|capture|prepare|probe|describe [runs] | write-model <new file>\n");
            return 2;
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "presentation_benchmark: %s\n", error.what());
        return 1;
    }
    return 0;
}
