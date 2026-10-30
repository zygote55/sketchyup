#include "automation/presentation_commands.hpp"
#include "blender_stand_in.hpp"
#include "dem_export_fixture.hpp"
#include "core/camera_motion.hpp"
#include "core/presentation.hpp"
#include "core/scenes.hpp"
#include "integrations/blender_job.hpp"
#include "integrations/presentation_batch.hpp"
#include "integrations/presentation_request.hpp"
#include "integrations/render_queue.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <utime.h>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read batch artifact");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
void waitUntil(const std::function<bool()> &condition, const char *message,
               int milliseconds = 120000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(condition(), message);
}
void wait(PresentationExport &batch) {
    waitUntil([&] { return batch.done(); }, "Batch finishes within its deadline");
}
// Runs queued events for a while, so an answer that would arrive has arrived.
void settle(int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
}
// A fake Blender that counts its launches. The launch numbered `hang` never returns, so a test can
// stop a frame while its worker runs; zero never hangs. A name such as bin/blender is the PATH
// entry.
QString launcher(const QTemporaryDir &files, const QString &name, const QString &counter,
                 int hang) {
    const QFileInfo target(files.filePath(name));
    QDir().mkpath(target.absolutePath());
    return blender_stand_in::countedAt(target.absoluteFilePath(), hang, counter);
}
int launches(const QString &counter) {
    return QFileInfo::exists(counter) ? read(counter).trimmed().toInt() : 0;
}
PresentationPlan plan(const std::vector<Id> &keyframes, CameraPathTiming timing = {24, 1, 1}) {
    PresentationPlan result;
    result.path = "Orbit";
    result.keyframes = keyframes;
    result.timing = timing;
    RenderSettings draft;
    draft.width = 64;
    draft.height = 64;
    draft.samples = 1;
    RenderSettings check;
    check.width = 80;
    check.height = 64;
    check.samples = 1;
    check.seed = 2;
    result.presets = {{"Draft", draft}, {"Check", check}};
    return result;
}
// Each preset's video record matches its file and its inputs, and the batch total counts every
// byte written: the frames, their metadata and the videos.
void verifyVideos(const QString &output, int expectedFrames, const char *codec = "h264") {
    const auto manifest = QJsonDocument::fromJson(read(output + "/manifest.json")).object();
    qint64 total = 0;
    for (const auto &presetValue : manifest.value("presets").toArray()) {
        const auto preset = presetValue.toObject();
        const auto directory = output + "/" + preset.value("directory").toString();
        const auto video = preset.value("video").toObject();
        const auto mp4 = read(directory + "/video.mp4");
        check(video.value("file") == "video.mp4" &&
                  video.value("bytes").toInteger() == mp4.size() &&
                  video.value("sha256").toString() == digest(mp4) &&
                  video.value("codec") == codec && video.value("container") == "mp4" &&
                  video.value("frames").toInteger() == expectedFrames &&
                  video.value("framesPerSecond").toInteger() == 24 &&
                  video.value("encoder").toString().startsWith("blender-") &&
                  video.value("inputSha256").toArray().size() == expectedFrames,
              "The video record matches its file and its input frames");
        total += mp4.size();
        for (const auto &entryValue : preset.value("frames").toArray()) {
            const auto name = entryValue.toObject().value("file").toString();
            total += read(directory + "/frames/" + name).size() +
                     read(directory + "/frames/" + name.left(name.size() - 4) + ".json").size();
        }
    }
    check(manifest.value("totalBytes").toInteger() == total,
          "The batch total counts the videos and every frame");
}
qint64 videoBytes(const QString &output, const QStringList &presets) {
    qint64 total = 0;
    for (const auto &preset : presets)
        total += QFileInfo(output + "/" + preset + "/video.mp4").size();
    return total;
}
// The codec and frame count that ffprobe reads back from a video, as "codec,frames".
QString probeVideo(const QString &path) {
    QProcess probe;
    probe.start("ffprobe",
                {"-v", "error", "-select_streams", "v:0", "-count_frames", "-show_entries",
                 "stream=codec_name,nb_read_frames", "-of", "csv=p=0", path});
    check(probe.waitForFinished(60000) && probe.exitCode() == 0,
          "ffprobe reads the video; install ffmpeg to run the real video test");
    return QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
}
// Every manifest frame entry is checked against its image and its metadata.
void verifyManifest(const QString &output, int expectedFrames) {
    const auto manifest = QJsonDocument::fromJson(read(output + "/manifest.json")).object();
    int frames = 0;
    for (const auto &presetValue : manifest.value("presets").toArray()) {
        const auto preset = presetValue.toObject();
        const auto directory = output + "/" + preset.value("directory").toString();
        for (const auto &entryValue : preset.value("frames").toArray()) {
            const auto entry = entryValue.toObject();
            const auto name = entry.value("file").toString();
            const auto png = read(directory + "/frames/" + name);
            check(digest(png) == entry.value("sha256").toString(),
                  "Manifest hash matches frame bytes");
            const auto metadata =
                QJsonDocument::fromJson(
                    read(directory + "/frames/" + name.left(name.size() - 4) + ".json"))
                    .object();
            check(metadata.value("sha256") == entry.value("sha256") &&
                      metadata.value("bytes").toInteger() == png.size() &&
                      metadata.value("sourceManifestSha256").isString() &&
                      metadata.value("result").isObject(),
                  "Per-frame metadata agrees with the manifest");
            ++frames;
        }
    }
    check(frames == expectedFrames, "Manifest lists every verified frame");
}
// Each frame's record keeps the camera its Blender job was given: the planned pose's camera, bit
// for bit, as the batch's manifest describes it. The planner's output is pinned in
// camera_motion_tests (ADR 0173 §6).
void verifyPoses(const QString &output, const std::vector<CameraFrame> &planned) {
    const auto manifest = QJsonDocument::fromJson(read(output + "/manifest.json")).object();
    int checked = 0;
    for (const auto &presetValue : manifest.value("presets").toArray()) {
        const auto directory = output + "/" + presetValue.toObject().value("directory").toString();
        for (size_t index = 0; index < planned.size(); ++index) {
            const auto name = QString("frame-%1").arg(index + 1, 4, 10, QChar('0'));
            const auto camera =
                QJsonDocument::fromJson(read(directory + "/frames/" + name + ".json"))
                    .object()
                    .value("camera")
                    .toObject();
            check(camera == describeRenderCamera(renderSceneCamera(planned[index].camera)),
                  "A frame's recorded camera is the planned pose, bit for bit");
            ++checked;
        }
    }
    check(checked == int(planned.size()) * manifest.value("presets").toArray().size(),
          "Every frame of every preset is checked");
}

// Largest deviation of each kind over every frame, in model units (a millimetre).
struct PoseDeviation {
    double position{}, target{}, up{}, clipping{}, fieldOfView{};
};

// The camera each frame's Blender rendered (the worker's renderedCamera, in our coordinates) is the
// planned pose to within 1 µm, which is 0.001 model units (ADR 0173 §6). The position is compared
// directly. The target and the up direction are compared where they land at the planned distance,
// so a turn of the camera is measured in the same units. The field of view is an angle: its error,
// times the planned distance, is how far the frame's edge moves at that distance, and it is held to
// the same 1 µm.
PoseDeviation verifyRenderedPoses(const QString &output, const std::vector<CameraFrame> &planned) {
    using Point = std::array<double, 3>;
    const auto along = [](const Point &from, const Point &direction, double distance) {
        return Point{from[0] + direction[0] * distance, from[1] + direction[1] * distance,
                     from[2] + direction[2] * distance};
    };
    const auto gap = [](const Point &a, const Point &b) {
        return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
    };
    const auto manifest = QJsonDocument::fromJson(read(output + "/manifest.json")).object();
    PoseDeviation worst;
    int checked = 0;
    for (const auto &presetValue : manifest.value("presets").toArray()) {
        const auto directory = output + "/" + presetValue.toObject().value("directory").toString();
        for (size_t index = 0; index < planned.size(); ++index) {
            const auto name = QString("frame-%1").arg(index + 1, 4, 10, QChar('0'));
            const auto rendered =
                QJsonDocument::fromJson(read(directory + "/frames/" + name + ".json"))
                    .object()
                    .value("result")
                    .toObject()
                    .value("renderedCamera")
                    .toObject();
            const auto values = rendered.value("matrix").toArray();
            check(values.size() == 16, "A frame records the 4 × 4 camera its Blender rendered");
            const auto cell = [&](int at) { return values[at].toDouble(); };
            // Columns are the x, y and z axes, then the position (renderCameraTransform's layout).
            const Point position{cell(12), cell(13), cell(14)}, up{cell(4), cell(5), cell(6)},
                forward{-cell(8), -cell(9), -cell(10)};
            const auto expected = renderSceneCamera(planned[index].camera);
            const auto basis = renderCameraTransform(expected).m;
            const Point plannedPosition{basis[12], basis[13], basis[14]},
                plannedUp{basis[4], basis[5], basis[6]};
            const double distance = std::sqrt(std::pow(expected.target.x - basis[12], 2) +
                                              std::pow(expected.target.y - basis[13], 2) +
                                              std::pow(expected.target.z - basis[14], 2));
            check(rendered.value("projection").toString() ==
                      (expected.orthographic ? "orthographic" : "perspective"),
                  "A frame's rendered projection is the planned one");
            const double positionError = gap(position, plannedPosition);
            const double targetError =
                gap(along(position, forward, distance),
                    {expected.target.x, expected.target.y, expected.target.z});
            const double upError =
                gap(along(position, up, distance), along(plannedPosition, plannedUp, distance));
            const double clippingError =
                std::max(std::abs(rendered.value("clipStart").toDouble() - expected.nearClip),
                         std::abs(rendered.value("clipEnd").toDouble() - expected.farClip));
            worst.position = std::max(worst.position, positionError);
            worst.target = std::max(worst.target, targetError);
            worst.up = std::max(worst.up, upError);
            worst.clipping = std::max(worst.clipping, clippingError);
            check(positionError <= 0.001, "A frame's rendered position is the planned one to 1 µm");
            check(targetError <= 0.001,
                  "A frame's rendered camera looks at the planned target to 1 µm");
            check(upError <= 0.001, "A frame's rendered up direction is the planned one to 1 µm");
            check(clippingError <= 0.001, "A frame's rendered clipping range is the planned one");
            if (!expected.orthographic) {
                const double fieldOfViewError =
                    std::abs(rendered.value("angleY").toDouble() - expected.verticalFov) * distance;
                worst.fieldOfView = std::max(worst.fieldOfView, fieldOfViewError);
                check(fieldOfViewError <= 0.001,
                      "A frame's rendered field of view moves its edge less than 1 µm");
            }
            ++checked;
        }
    }
    check(checked == int(planned.size()) * manifest.value("presets").toArray().size(),
          "Every rendered frame of every preset is checked");
    return worst;
}

void reportDeviation(const char *label, const PoseDeviation &worst) {
    std::cout << "Rendered pose, " << label
              << ", largest deviation over every frame (model units): position " << worst.position
              << ", target " << worst.target << ", up " << worst.up << ", clipping "
              << worst.clipping << ", field of view edge " << worst.fieldOfView << "\n";
}
// Two batches of one capture write the same frame records, apart from their output folders: the
// source manifest, the camera, the scene and the keyframe flag of each frame are equal.
void verifySameFrames(const QString &output, const QString &baseline, int frames) {
    for (const auto *preset : {"01-Draft", "02-Check"})
        for (int number = 1; number <= frames; ++number) {
            const auto name =
                QString("/%1/frames/frame-%2.json").arg(preset).arg(number, 4, 10, QChar('0'));
            const auto live = QJsonDocument::fromJson(read(output + name)).object();
            const auto frozen = QJsonDocument::fromJson(read(baseline + name)).object();
            check(live.value("sourceManifestSha256") == frozen.value("sourceManifestSha256") &&
                      live.value("camera") == frozen.value("camera") &&
                      live.value("scene") == frozen.value("scene") &&
                      live.value("keyframe") == frozen.value("keyframe"),
                  "A frame matches the batch that ran on the captured state");
        }
}
StoredRenderJob renderRecord(const RenderQueue &queue, const QString &id) {
    for (const auto &job : queue.jobs())
        if (job.id == id)
            return job;
    throw std::runtime_error("The render job is not stored");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool real = app.arguments().contains("--real");
    if (real && qEnvironmentVariableIsEmpty("SKETCHYUP_BLENDER_TEST"))
        return 77;
    try {
        QTemporaryDir files;
        check(files.isValid(), "Batch fixture directory");
        // The stand-ins count their launches in this directory (tests/blender_stand_in.hpp).
        setBlenderTestWritableDirectories({files.path()});
        const PresentationJobStore::Limits limits{8, 16, 256LL * 1024 * 1024};
        const auto store = files.filePath("store");
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        SceneSnapshot view;
        view.camera = SceneCamera{};
        const auto first = createScene(doc, "A", view);
        view.camera->yaw = 90;
        const auto second = createScene(doc, "B", view);
        view.camera->yaw = 180;
        const auto third = createScene(doc, "C", view);
        const std::vector<Id> keyframes{first, second, third};
        BlenderJob::Options options;
        options.timeoutMs = 60000;
        auto blender = [&](const QString &counter, int hang = 0) {
            BlenderJob::Options launched = options;
            launched.executable =
                launcher(files, "blender-" + QFileInfo(counter).fileName(), counter, hang);
            return launched;
        };

        // One capture: both presets render from the frozen document, and a live edit made while
        // the batch runs changes none of its frames.
        {
            PresentationJobStore jobs(store, limits);
            const auto capture = PresentationCapture::capture(doc, plan(keyframes));
            const auto baseline = PresentationCapture::capture(doc, plan(keyframes));
            check(capture.sha256() == digest(capture.encoded()) && capture.frames().size() == 6,
                  "Capture plans six frames for three keyframes at one hold and one transition");
            int keyframeFrames = 0;
            for (const auto &frame : capture.frames())
                keyframeFrames += frame.keyframe;
            check(keyframeFrames == 3, "Every keyframe pose is one flagged planned frame");
            PresentationExport batch(jobs);
            const auto output = files.filePath("complete");
            const auto id = batch.start(capture, output, blender(files.filePath("count-complete")));
            doc.addFace({{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}});
            const auto edited = encodeDocument(doc);
            wait(batch);
            check(batch.state() == PresentationJobState::Completed &&
                      batch.completedFrames() == 12 && batch.totalFrames() == 12,
                  "Both presets complete every planned frame");
            check(jobs.jobs().at(id).state == PresentationJobState::Completed,
                  "The store records the completed batch");
            const auto manifest = QJsonDocument::fromJson(read(output + "/manifest.json")).object();
            check(manifest.value("state") == "completed" &&
                      manifest.value("captureSha256") == capture.sha256() &&
                      manifest.value("framesPerSecond") == 24 &&
                      manifest.value("framesPerPreset") == 6 &&
                      manifest.value("presets").toArray().size() == 2,
                  "The batch manifest pins the capture, timing and preset layout");
            verifyManifest(output, 12);
            verifyPoses(output, capture.frames());
            reportDeviation("stand-in", verifyRenderedPoses(output, capture.frames()));
            check(QFileInfo(output + "/01-Draft/frames/frame-0001.png").isFile() &&
                      QFileInfo(output + "/02-Check/frames/frame-0006.png").isFile(),
                  "Each preset writes its own numbered frame folder");

            PresentationExport twin(jobs);
            const auto twinOutput = files.filePath("baseline");
            twin.start(baseline, twinOutput, blender(files.filePath("count-baseline")));
            wait(twin);
            check(twin.state() == PresentationJobState::Completed, "Baseline batch completes");
            for (const auto *preset : {"01-Draft", "02-Check"})
                for (int number = 1; number <= 6; ++number) {
                    const auto name = QString("/%1/frames/frame-%2.json")
                                          .arg(preset)
                                          .arg(number, 4, 10, QChar('0'));
                    const auto live = QJsonDocument::fromJson(read(output + name)).object();
                    const auto frozen = QJsonDocument::fromJson(read(twinOutput + name)).object();
                    check(live.value("sourceManifestSha256") ==
                                  frozen.value("sourceManifestSha256") &&
                              live.value("camera") == frozen.value("camera") &&
                              live.value("scene") == frozen.value("scene") &&
                              live.value("keyframe") == frozen.value("keyframe"),
                          "A live edit during a batch leaves its frames unchanged");
                }
            check(encodeDocument(doc) == edited, "The engine leaves the live document unchanged");
        }

        // One engine runs two batches in turn: the second starts from its own cursor and totals.
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                        files.filePath("reuse-one"), blender(files.filePath("count-reuse-one")));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "The first batch on a reused engine completes");
            batch.start(PresentationCapture::capture(doc, plan({first, second})),
                        files.filePath("reuse-two"), blender(files.filePath("count-reuse-two")));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed &&
                      batch.completedFrames() == 8 && batch.totalFrames() == 8,
                  "The second batch on a reused engine renders its own frames");
            verifyManifest(files.filePath("reuse-two"), 8);
        }

        // Cancel at a frame boundary: finished frames stay, nothing past the boundary is written.
        const auto canceled = files.filePath("canceled");
        QString canceledId;
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            canceledId = batch.start(PresentationCapture::capture(doc, plan(keyframes)), canceled,
                                     blender(files.filePath("count-canceled")));
            bool requested = false;
            QObject::connect(&batch, &PresentationExport::changed, &app, [&] {
                if (!requested && batch.completedFrames() == 2) {
                    requested = true;
                    batch.cancel();
                }
            });
            wait(batch);
            check(batch.state() == PresentationJobState::Canceled && batch.completedFrames() == 2,
                  "Cancel at a frame boundary keeps the two finished frames");
            check(!QFileInfo::exists(canceled + "/01-Draft/frames/frame-0003.png") &&
                      !QFileInfo::exists(canceled + "/01-Draft/frames/frame-0003.json") &&
                      QFileInfo::exists(canceled + "/01-Draft/frames/frame-0002.png"),
                  "A canceled batch writes no frame past the boundary");
            const auto manifest =
                QJsonDocument::fromJson(read(canceled + "/manifest.json")).object();
            check(manifest.value("state") == "canceled" && manifest.value("completedFrames") == 2,
                  "The canceled manifest records the frames kept");
            verifyManifest(canceled, 2);
            check(jobs.jobs().at(canceledId).state == PresentationJobState::Canceled,
                  "The store keeps the canceled batch with its state");
        }

        // Cancel while a frame runs: the in-flight frame is discarded and the finished one stays.
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            const auto midFrame = files.filePath("mid-frame");
            const auto counter = files.filePath("count-mid-frame");
            batch.start(PresentationCapture::capture(doc, plan(keyframes)), midFrame,
                        blender(counter, 2));
            waitUntil([&] { return launches(counter) == 2; }, "The second frame worker starts");
            batch.cancel();
            wait(batch);
            check(batch.state() == PresentationJobState::Canceled && batch.completedFrames() == 1,
                  "Cancel while a frame runs keeps only the finished frame");
            check(!QFileInfo::exists(midFrame + "/01-Draft/frames/frame-0002.png") &&
                      !QFileInfo::exists(midFrame + "/01-Draft/frames/frame-0002.json"),
                  "The in-flight frame writes no file");
            verifyManifest(midFrame, 1);
        }

        // Retry resumes from the first unverified frame and never re-renders a verified frame.
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            const auto counter = files.filePath("count-resume");
            const auto kept = read(canceled + "/01-Draft/frames/frame-0001.png");
            batch.retry(canceledId, blender(counter));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "Retry completes the canceled batch");
            check(launches(counter) == 10,
                  "Retry renders only the ten frames that were not verified");
            check(read(canceled + "/01-Draft/frames/frame-0001.png") == kept,
                  "A verified frame is byte-identical after retry");
            verifyManifest(canceled, 12);
        }

        // A damaged verified frame is the only one that re-renders besides the unrendered frames.
        {
            PresentationJobStore jobs(store, limits);
            const auto corrupt = files.filePath("corrupt");
            PresentationExport batch(jobs);
            bool requested = false;
            QObject::connect(&batch, &PresentationExport::changed, &app, [&] {
                if (!requested && batch.completedFrames() == 4) {
                    requested = true;
                    batch.cancel();
                }
            });
            const auto id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), corrupt,
                                        blender(files.filePath("count-corrupt")));
            wait(batch);
            check(batch.state() == PresentationJobState::Canceled && batch.completedFrames() == 4,
                  "Canceled after four frames");
            write(corrupt + "/01-Draft/frames/frame-0002.png", "damaged image");
            const auto counter = files.filePath("count-corrupt-retry");
            batch.retry(id, blender(counter));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && launches(counter) == 9,
                  "Only the damaged frame and the unrendered frames run on retry");
            verifyManifest(corrupt, 12);
        }

        // A stopped worker: the engine closes, the batch reopens interrupted and resumes.
        {
            const auto stopped = files.filePath("stopped");
            const auto counter = files.filePath("count-stopped");
            QString id;
            {
                PresentationJobStore jobs(store, limits);
                PresentationExport batch(jobs);
                id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), stopped,
                                 blender(counter, 2));
                waitUntil([&] { return launches(counter) == 2; }, "The second frame worker starts");
                waitUntil([&] { return batch.completedFrames() == 1; }, "The first frame is kept");
            }
            PresentationJobStore reopened(store, limits);
            check(reopened.jobs().at(id).state == PresentationJobState::Interrupted,
                  "A stopped batch reopens as interrupted");
            PresentationExport batch(reopened);
            batch.retry(id, blender(files.filePath("count-stopped-retry")));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "An interrupted batch resumes to completion");
            verifyManifest(stopped, 12);
        }

        // A damaged manifest refuses the retry and keeps every finished frame.
        {
            PresentationJobStore jobs(store, limits);
            PresentationExport batch(jobs);
            const auto damaged = files.filePath("damaged-manifest");
            bool requested = false;
            QObject::connect(&batch, &PresentationExport::changed, &app, [&] {
                if (!requested && batch.completedFrames() == 2) {
                    requested = true;
                    batch.cancel();
                }
            });
            const auto id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), damaged,
                                        blender(files.filePath("count-damaged")));
            wait(batch);
            write(damaged + "/manifest.json", "{ broken");
            const auto kept = read(damaged + "/01-Draft/frames/frame-0001.png");
            rejects([&] { batch.retry(id, blender(files.filePath("count-damaged-retry"))); },
                    "A damaged manifest refuses retry");
            check(batch.state() == PresentationJobState::Canceled &&
                      read(damaged + "/01-Draft/frames/frame-0001.png") == kept,
                  "A refused retry leaves the finished frames as they were");
        }

        // Refusals: each one names its reason, and leaves no folder and no job behind.
        {
            PresentationJobStore jobs(store, limits);
            const auto count = jobs.jobs().size();
            PresentationExport batch(jobs);
            const auto refused = files.filePath("refused");
            Document plain;
            plain.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
            SceneSnapshot noCamera;
            noCamera.visibility = SceneVisibility{};
            const auto withoutCamera = createScene(plain, "Plain", noCamera);
            rejects(
                [&] { PresentationCapture::capture(plain, plan({withoutCamera, withoutCamera})); },
                "A keyframe without a camera is refused");
            rejects([&] { PresentationCapture::capture(doc, plan({first, 999999})); },
                    "A keyframe scene that is gone is refused");
            rejects([&] { PresentationCapture::capture(doc, plan({first})); },
                    "A path needs two keyframes");
            auto five = plan(keyframes);
            five.presets.push_back({"Extra", {}});
            five.presets.push_back({"Again", {}});
            five.presets.push_back({"Third", {}});
            rejects([&] { PresentationCapture::capture(doc, five); },
                    "Over four presets is refused");
            rejects([&] { PresentationCapture::capture(doc, plan(keyframes, {24, 24, 80})); },
                    "A plan over 240 frames is refused");

            const auto overBudget =
                plan({first, second}, {24, 25, 107}); // 1 + 2 x 107 + 25 frames.
            check(PresentationCapture::capture(doc, overBudget).frames().size() == 240,
                  "A two-keyframe path plans 240 frames");

            batch.setFreeSpaceProbe([](const QString &) { return qint64(0); });
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, plan(keyframes)), refused,
                                blender(files.filePath("count-disk")));
                },
                "A batch without free disk space is refused");
            check(!QFileInfo::exists(refused) && jobs.jobs().size() == count,
                  "A disk refusal leaves no folder and no job");
            batch.setFreeSpaceProbe([](const QString &) { return qint64(1) << 50; });

            const auto existing = files.filePath("existing");
            check(QDir().mkpath(existing), "Existing folder fixture");
            write(existing + "/keep.txt", "keep");
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, plan(keyframes)), existing,
                                blender(files.filePath("count-existing")));
                },
                "An existing destination is refused");
            check(read(existing + "/keep.txt") == "keep" && jobs.jobs().size() == count,
                  "An existing destination is left as it was");

            const auto target = files.filePath("link-target");
            check(QDir().mkpath(target) && QFile::link(target, files.filePath("linked-output")),
                  "Symbolic link fixture");
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                                files.filePath("linked-output"),
                                blender(files.filePath("count-link")));
                },
                "A symbolic link destination is refused");
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                                files.filePath("missing/parent/out"),
                                blender(files.filePath("count-parent")));
                },
                "A destination needs an existing parent");
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, plan(keyframes)), "relative/out",
                                blender(files.filePath("count-relative")));
                },
                "A destination must be absolute");

            BlenderJob::Options missing = options;
            missing.executable = files.filePath("no-such-blender");
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, plan(keyframes)), refused,
                                missing);
                },
                "A missing Blender executable is refused");

            auto eevee = plan(keyframes);
            eevee.presets[0].settings.engine = RenderEngine::Eevee;
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, eevee), refused,
                                blender(files.filePath("count-backend")));
                },
                "Eevee presets need an OpenGL device");
            check(!QFileInfo::exists(refused) && jobs.jobs().size() == count,
                  "Each refusal leaves no folder and no job");
        }

        // One batch at a time: a second start while one runs is refused.
        {
            PresentationJobStore jobs(files.filePath("single"), limits);
            PresentationExport batch(jobs);
            const auto id = batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                                        files.filePath("single-running"),
                                        blender(files.filePath("count-single"), 1));
            rejects(
                [&] {
                    batch.start(PresentationCapture::capture(doc, plan(keyframes)),
                                files.filePath("single-second"),
                                blender(files.filePath("count-single-2")));
                },
                "A second batch cannot start while one runs");
            check(!QFileInfo::exists(files.filePath("single-second")),
                  "A refused second batch creates no folder");
            batch.cancel();
            wait(batch);
            check(jobs.jobs().at(id).state == PresentationJobState::Canceled,
                  "A canceled running batch ends canceled");
        }

        // The output budget is checked per frame, not refused on the worst-case PNG bound.
        {
            PresentationJobStore jobs(files.filePath("budget"), limits);
            PresentationExport batch(jobs);
            batch.setFreeSpaceProbe([](const QString &) { return qint64(1) << 50; });
            auto large = plan({first, second}, {24, 25, 107});
            for (auto &preset : large.presets)
                preset.settings = {4096, 4096, 32, 0, RenderEngine::Cycles};
            const auto accepted = batch.start(PresentationCapture::capture(doc, large),
                                              files.filePath("budget-large"),
                                              blender(files.filePath("count-budget-large"), 1));
            check(batch.totalFrames() == 480,
                  "A batch whose worst-case PNG bound passes 2 GiB still starts");
            batch.cancel();
            wait(batch);
            check(jobs.jobs().at(accepted).state == PresentationJobState::Canceled,
                  "The accepted large batch cancels cleanly");

            const auto over = files.filePath("budget-over");
            batch.setOutputLimit(1);
            batch.start(PresentationCapture::capture(doc, plan(keyframes)), over,
                        blender(files.filePath("count-budget-over")));
            wait(batch);
            check(batch.state() == PresentationJobState::Failed && batch.completedFrames() == 0 &&
                      batch.message().contains("remain"),
                  "A frame over the output budget fails the batch with the remaining budget");
            check(!QFileInfo::exists(over + "/01-Draft/frames/frame-0001.png"),
                  "The frame over the budget is not written");
            check(QJsonDocument::fromJson(read(over + "/manifest.json")).object().value("state") ==
                      "failed",
                  "The manifest records the budget failure");
        }

        // Video (decision 7): every preset's verified frames encode to one MP4 after all frames are
        // verified. The fake encoder writes a synthetic MP4 with the real sample table layout.
        {
            PresentationJobStore jobs(store, limits);
            auto videoPlan = plan(keyframes);
            videoPlan.video = true;
            const auto capture = PresentationCapture::capture(doc, videoPlan);
            auto odd = videoPlan;
            odd.presets[1].settings.width = 81;
            rejects([&] { PresentationCapture::capture(doc, odd); },
                    "A video needs even preset dimensions");
            const QStringList presets{"01-Draft", "02-Check"};

            const auto output = files.filePath("video");
            const auto counter = files.filePath("count-video");
            PresentationExport batch(jobs);
            const auto id = batch.start(capture, output, blender(counter));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "A video batch completes every frame before it encodes");
            check(
                launches(counter) == 15,
                "The start probe runs once, then each preset encodes once after its twelve frames");
            check(jobs.jobs().at(id).plan.video, "The store keeps the video request");
            verifyVideos(output, 6);
            verifyManifest(output, 12);

            // A cancel during an encode keeps every frame and writes no video.
            const auto canceledOutput = files.filePath("video-canceled");
            const auto stalled = files.filePath("count-video-canceled");
            PresentationExport canceled(jobs);
            bool stopped = false;
            QObject::connect(&canceled, &PresentationExport::changed, &app, [&] {
                if (!stopped && canceled.completedFrames() == 12 &&
                    canceled.message().startsWith("Encoding video")) {
                    stopped = true;
                    canceled.cancel();
                }
            });
            const auto canceledId = canceled.start(capture, canceledOutput, blender(stalled, 14));
            wait(canceled);
            check(canceled.state() == PresentationJobState::Canceled &&
                      canceled.completedFrames() == 12,
                  "Canceling during an encode keeps every verified frame");
            for (const auto &preset : presets)
                check(!QFileInfo::exists(canceledOutput + "/" + preset + "/video.mp4"),
                      "A canceled batch writes no video");
            verifyManifest(canceledOutput, 12);

            // Retry encodes the canceled batch's presets without rendering any verified frame.
            PresentationExport resumed(jobs);
            const auto resumeCounter = files.filePath("count-video-resume");
            resumed.retry(canceledId, blender(resumeCounter));
            wait(resumed);
            check(resumed.state() == PresentationJobState::Completed &&
                      launches(resumeCounter) == 3,
                  "Retry probes once, encodes both presets and renders no verified frame");
            verifyVideos(canceledOutput, 6);
            verifyManifest(canceledOutput, 12);

            // A Blender whose encoder refuses at encode time, though its probe listed it, fails the
            // batch with its reason and no code in the text; the frames stay and no second encode
            // starts.
            const auto refused = files.filePath("video-refused");
            const auto refusedCounter = files.filePath("count-video-refused");
            qputenv("SKETCHYUP_FAKE_ENCODE", "codec_unavailable");
            PresentationExport withoutCodec(jobs);
            const auto refusedId = withoutCodec.start(capture, refused, blender(refusedCounter));
            wait(withoutCodec);
            qunsetenv("SKETCHYUP_FAKE_ENCODE");
            check(withoutCodec.state() == PresentationJobState::Failed &&
                      withoutCodec.message() ==
                          "Video not written for Draft: This Blender's FFmpeg "
                          "build has no H.264 encoder. The finished frames are kept.",
                  "A video refused at encode time names its reason, with no code, in its message");
            check(jobs.jobs().at(refusedId).report.value("code") == "codec_unavailable",
                  "The stored report keeps the worker's code apart from the text");
            check(withoutCodec.completedFrames() == 12 && launches(refusedCounter) == 14,
                  "The refusal keeps every frame and starts no second encode");
            for (const auto &preset : presets)
                check(!QFileInfo::exists(refused + "/" + preset + "/video.mp4"),
                      "A refused video writes no file");
            verifyManifest(refused, 12);

            // A frame that changes before its own encode is refused by the worker's hash check.
            const auto damaged = files.filePath("video-damaged");
            qputenv("SKETCHYUP_FAKE_DAMAGE",
                    (damaged + "/01-Draft/frames/frame-0001.png").toUtf8());
            PresentationExport changedFrame(jobs);
            changedFrame.start(capture, damaged, blender(files.filePath("count-video-damaged")));
            wait(changedFrame);
            qunsetenv("SKETCHYUP_FAKE_DAMAGE");
            check(changedFrame.state() == PresentationJobState::Failed &&
                      changedFrame.message().contains("A frame changed before encoding") &&
                      !changedFrame.message().contains("invalid_frame"),
                  "A frame that changes before its encode blocks the video, with no code in the "
                  "text");
            check(!QFileInfo::exists(damaged + "/01-Draft/video.mp4"),
                  "A blocked video writes no file");

            // A frame of a later preset that changes during an earlier encode is caught before
            // that preset's encode starts.
            const auto later = files.filePath("video-later");
            qputenv("SKETCHYUP_FAKE_DAMAGE", (later + "/02-Check/frames/frame-0001.png").toUtf8());
            PresentationExport changedLater(jobs);
            changedLater.start(capture, later, blender(files.filePath("count-video-later")));
            wait(changedLater);
            qunsetenv("SKETCHYUP_FAKE_DAMAGE");
            check(changedLater.state() == PresentationJobState::Failed &&
                      changedLater.message().contains("02-Check changed after it was verified"),
                  "A later preset's frame that changes is named before its encode");
            check(QFileInfo::exists(later + "/01-Draft/video.mp4") &&
                      !QFileInfo::exists(later + "/02-Check/video.mp4"),
                  "The earlier preset keeps its verified video; the later one has none");

            // The video counts against the output budget: a limit that fits every frame but not the
            // videos refuses the video and keeps the frames.
            const auto frameBytes = QJsonDocument::fromJson(read(output + "/manifest.json"))
                                        .object()
                                        .value("totalBytes")
                                        .toInteger() -
                                    videoBytes(output, presets);
            PresentationExport tight(jobs);
            tight.setOutputLimit(frameBytes);
            const auto tightOutput = files.filePath("video-budget");
            // The frame metadata records the launcher's path, so this run uses the reference's
            // launcher name and its frames are the same size as the reference frames.
            tight.start(capture, tightOutput, blender(counter));
            wait(tight);
            check(tight.state() == PresentationJobState::Failed &&
                      tight.message().contains("budget") && tight.completedFrames() == 12,
                  "The video counts against the output budget and keeps the frames");
            check(videoBytes(tightOutput, presets) == 0, "An over-budget video is not written");
        }

        // A video checks the Blender's codec list before anything is written (UX_DESIGN 17.5). A
        // build without the chosen encoder, or a probe that cannot be read, refuses with its
        // reason; no folder, job or frame exists, and only the probe ran. The refusal names no
        // code.
        {
            PresentationJobStore jobs(files.filePath("probe-store"), limits);
            const auto jobsBefore = jobs.jobs().size();
            auto videoPlan = plan(keyframes);
            videoPlan.video = true;
            const auto refuse = [&](const char *build, PresentationVideoCodec codec,
                                    const QString &name) {
                auto chosen = videoPlan;
                chosen.videoCodec = codec;
                const auto counter = files.filePath("count-" + name);
                qputenv("SKETCHYUP_FAKE_CODECS", build);
                std::string message;
                PresentationExport batch(jobs);
                try {
                    batch.start(PresentationCapture::capture(doc, chosen), files.filePath(name),
                                blender(counter));
                } catch (const std::exception &error) {
                    message = error.what();
                }
                qunsetenv("SKETCHYUP_FAKE_CODECS");
                check(!message.empty() && !QFileInfo::exists(files.filePath(name)) &&
                          jobs.jobs().size() == jobsBefore,
                      "A refused video leaves no folder and no job");
                check(launches(counter) == 1, "Only the codec probe runs before a video refusal");
                return message;
            };
            check(refuse("no-h264", PresentationVideoCodec::H264, "refuse-h264") ==
                      "Video is not available in this Blender: its FFmpeg build has no H.264 "
                      "encoder. "
                      "No frames were rendered. Clear Write video to render the frames only.",
                  "A build without H.264 refuses an H.264 video with the 17.5 wording, before a "
                  "frame");
            check(
                refuse("no-mpeg4", PresentationVideoCodec::MPEG4, "refuse-mpeg4") ==
                    "Video is not available in this Blender: its FFmpeg build has no MPEG-4 Part 2 "
                    "encoder. No frames were rendered. Clear Write video to render the frames "
                    "only.",
                "A build without MPEG-4 refuses an explicit MPEG-4 video with the same wording");
            check(refuse("absent", PresentationVideoCodec::H264, "refuse-absent") ==
                      "The video check did not report this Blender's codecs. No frames were "
                      "rendered. "
                      "Check again, or clear Write video to render the frames only.",
                  "A probe that reports no codec list refuses rather than assuming a codec");
            // Start's refusal names the codec the batch chose (the dialog shows this text).
            check(presentationVideoRefusal(PresentationVideoCodec::H264) ==
                      "H.264 is not in this Blender's FFmpeg build, so no video can be written. "
                      "Clear Write video to render the frames only.",
                  "An H.264 batch's Start refusal names H.264");
            check(presentationVideoRefusal(PresentationVideoCodec::MPEG4) ==
                      "MPEG-4 Part 2 is not in this Blender's FFmpeg build, so no video can be "
                      "written. Clear Write video to render the frames only.",
                  "An MPEG-4 batch's Start refusal names MPEG-4 Part 2, not H.264");
            check(refuse("malformed", PresentationVideoCodec::H264, "refuse-malformed") ==
                      "The video check did not finish. No frames were rendered. Check again, or "
                      "clear "
                      "Write video to render the frames only.",
                  "A malformed codec list refuses the video");
        }

        // An explicit MPEG-4 Part 2 batch on a build without H.264: the codec is used only because
        // it was named. Each preset encodes as mpeg4, and the manifest and the store record the
        // choice.
        {
            PresentationJobStore jobs(files.filePath("mpeg4-store"), limits);
            auto mpeg4Plan = plan(keyframes);
            mpeg4Plan.video = true;
            mpeg4Plan.videoCodec = PresentationVideoCodec::MPEG4;
            const auto output = files.filePath("video-mpeg4");
            const auto counter = files.filePath("count-video-mpeg4");
            qputenv("SKETCHYUP_FAKE_CODECS", "no-h264");
            PresentationExport batch(jobs);
            const auto id =
                batch.start(PresentationCapture::capture(doc, mpeg4Plan), output, blender(counter));
            wait(batch);
            qunsetenv("SKETCHYUP_FAKE_CODECS");
            check(batch.state() == PresentationJobState::Completed &&
                      batch.completedFrames() == 12 && launches(counter) == 15,
                  "An explicit MPEG-4 batch completes on a build without H.264");
            check(jobs.jobs().at(id).plan.videoCodec == PresentationVideoCodec::MPEG4,
                  "The store keeps the MPEG-4 choice");
            verifyVideos(output, 6, "mpeg4");
            verifyManifest(output, 12);
            check(QJsonDocument::fromJson(read(output + "/manifest.json"))
                          .object()
                          .value("videoCodec") == "mpeg4",
                  "The manifest names the video codec");
        }

        // The frame limit is refused with the record's one message, formula included
        // (UX_DESIGN 17.11).
        {
            std::string message;
            try {
                PresentationCapture::capture(doc, plan(keyframes, {24, 24, 80}));
            } catch (const std::exception &error) {
                message = error.what();
            }
            check(message ==
                      "Camera path would plan 289 frames (1 + 3 x 80 + 2 x 24); the limit is 240",
                  "A path over the frame limit is refused with the record's one message");
        }

        // The per-frame metadata keeps one copy of the attempt log (UX_DESIGN 17.11, gap 7). A
        // worker whose log reaches the 64 KiB cap, as a large scene's does, gives frames within one
        // copy of it.
        {
            PresentationJobStore jobs(files.filePath("metadata-store"), limits);
            const auto output = files.filePath("metadata");
            qputenv("SKETCHYUP_FAKE_LOG", "64k");
            PresentationExport batch(jobs);
            batch.start(PresentationCapture::capture(doc, plan({first, second}, {24, 1, 0})),
                        output, blender(files.filePath("count-metadata")));
            wait(batch);
            qunsetenv("SKETCHYUP_FAKE_LOG");
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 4,
                  "The metadata batch completes its four frames");
            qint64 largest = 0;
            for (const auto *preset : {"01-Draft", "02-Check"})
                for (const auto *number : {"0001", "0002"}) {
                    const auto text =
                        read(output + "/" + preset + "/frames/frame-" + number + ".json");
                    const auto metadata = QJsonDocument::fromJson(text).object();
                    largest = std::max<qint64>(largest, text.size());
                    check(QString::fromUtf8(text).count(QStringLiteral("\"logTail\"")) == 1,
                          "One copy of the attempt log is kept per frame");
                    check(!metadata.value("result").toObject().contains("attempts") &&
                              metadata.value("diagnostics")
                                      .toObject()
                                      .value("attempts")
                                      .toArray()
                                      .size() == 1,
                          "The attempt log sits under diagnostics, and the result keeps none");
                    check(text.size() <= 80 * 1024,
                          "A frame's metadata stays within one log's size");
                }
            std::cout << "Per-frame metadata with a 64 KiB attempt log: " << largest << " bytes\n";
        }

        // Stop (UX_DESIGN 17.12 choice 5): the batch stops now. The worker dies, its frame is
        // discarded, the record reads interrupted at once, and a retry resumes at the first
        // unverified frame.
        {
            PresentationJobStore jobs(files.filePath("stop-store"), limits);
            const auto stopped = files.filePath("stop-now");
            const auto counter = files.filePath("count-stop-now");
            PresentationExport batch(jobs);
            const auto id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), stopped,
                                        blender(counter, 2));
            waitUntil([&] { return launches(counter) == 2; }, "The second frame worker starts");
            waitUntil([&] { return batch.completedFrames() == 1; }, "The first frame is kept");
            batch.stop();
            check(batch.done() && batch.state() == PresentationJobState::Interrupted &&
                      jobs.jobs().at(id).state == PresentationJobState::Interrupted,
                  "Stop records the batch interrupted at once");
            check(batch.message().contains("Resume") && !batch.message().contains("Retry"),
                  "The interrupted message names Resume, in the interface's words");
            check(QFileInfo::exists(stopped + "/01-Draft/frames/frame-0001.png") &&
                      !QFileInfo::exists(stopped + "/01-Draft/frames/frame-0002.png") &&
                      !QFileInfo::exists(stopped + "/01-Draft/frames/frame-0002.json"),
                  "Stop keeps the finished frame and writes none of the one in flight");
            check(
                QJsonDocument::fromJson(read(stopped + "/manifest.json")).object().value("state") ==
                    "interrupted",
                "The manifest records the stop");
            verifyManifest(stopped, 1);
            const auto frozen = launches(counter);
            QThread::msleep(300);
            check(launches(counter) == frozen, "A stopped batch starts no further worker");
            PresentationExport resumed(jobs);
            const auto retryCounter = files.filePath("count-stop-now-retry");
            resumed.retry(id, blender(retryCounter));
            wait(resumed);
            check(resumed.state() == PresentationJobState::Completed &&
                      resumed.completedFrames() == 12 && launches(retryCounter) == 11,
                  "Retry resumes at the first unverified frame and renders the other eleven");
            verifyManifest(stopped, 12);
        }

        // Closing the engine while a batch runs records it interrupted before anything reopens the
        // store.
        {
            const auto storeDirectory = files.filePath("close-store");
            const auto closed = files.filePath("closed");
            const auto counter = files.filePath("count-closed");
            QString id;
            {
                PresentationJobStore jobs(storeDirectory, limits);
                PresentationExport batch(jobs);
                id = batch.start(PresentationCapture::capture(doc, plan(keyframes)), closed,
                                 blender(counter, 2));
                waitUntil([&] { return launches(counter) == 2; }, "The second frame worker starts");
                waitUntil([&] { return batch.completedFrames() == 1; }, "The first frame is kept");
            }
            check(QJsonDocument::fromJson(read(storeDirectory + "/" + id + "/job.json"))
                          .object()
                          .value("state") == "interrupted",
                  "Closing a running batch writes it interrupted at once");
            PresentationJobStore reopened(storeDirectory, limits);
            PresentationExport batch(reopened);
            batch.retry(id, blender(files.filePath("count-closed-retry")));
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "A batch closed while it ran resumes to completion");
        }

        // The codec check is cached per executable (UX_DESIGN 17.5). A second check launches
        // nothing; a changed modification time or size is probed again.
        {
            std::chrono::steady_clock::time_point now{};
            PresentationVideoChecks checks;
            checks.setClock([&now] { return now; });
            int finished = 0;
            QObject::connect(&checks, &PresentationVideoChecks::finished, &app,
                             [&finished] { ++finished; });
            const auto counter = files.filePath("count-cache");
            const auto probed = blender(counter);
            check(!checks.cached(probed).has_value(), "Nothing is cached before a check");
            const auto first = checks.check(probed);
            check(first.checked && first.codecs.contains("H264") && launches(counter) == 1,
                  "The first check probes the Blender once");
            check(checks.check(probed).checked && launches(counter) == 1 &&
                      checks.cached(probed).has_value(),
                  "A second check reuses the cached answer and launches nothing");
            now += std::chrono::hours(1);
            check(checks.cached(probed).has_value(),
                  "A successful answer is reused until the file changes");

            struct utimbuf older{1000000000, 1000000000};
            check(utime(probed.executable.toUtf8().constData(), &older) == 0,
                  "Set an older modification time on the fixture");
            check(!checks.cached(probed).has_value(),
                  "A changed modification time ends the cached answer");
            checks.refresh(probed);
            waitUntil([&] { return finished == 1; }, "A refresh probes the changed file");
            check(launches(counter) == 2 && checks.cached(probed).has_value(),
                  "The changed file is probed once and its answer is cached");

            write(probed.executable, read(probed.executable) + "\n");
            check(!checks.cached(probed).has_value(), "A changed size ends the cached answer");
            checks.refresh(probed);
            waitUntil([&] { return finished == 2; }, "A refresh probes the resized file");
            check(launches(counter) == 3, "The resized file is probed once more");
        }

        // A failed answer is kept for its 60 s window, so a broken Blender is not launched on every
        // call. After the window it is probed again; a forced refresh ("Check again") probes at
        // once.
        {
            std::chrono::steady_clock::time_point now{};
            PresentationVideoChecks checks;
            checks.setClock([&now] { return now; });
            int finished = 0;
            QObject::connect(&checks, &PresentationVideoChecks::finished, &app,
                             [&finished] { ++finished; });
            const auto counter = files.filePath("count-failed");
            const auto broken = blender(counter);
            qputenv("SKETCHYUP_FAKE_CODECS", "absent");
            check(!checks.check(broken).checked && launches(counter) == 1,
                  "A probe that reports no codec list fails once");
            check(!checks.check(broken).checked && launches(counter) == 1,
                  "A failed answer is reused inside its retry window");
            now += std::chrono::seconds(59);
            checks.refresh(broken);
            settle(200);
            check(launches(counter) == 1 && finished == 0,
                  "A refresh inside the retry window launches nothing");
            now += std::chrono::seconds(2);
            check(!checks.cached(broken).has_value(),
                  "A failed answer ends at its 60 s retry window");
            checks.refresh(broken);
            waitUntil([&] { return finished == 1; }, "A refresh after the window probes again");
            check(launches(counter) == 2, "The failed Blender is probed again after its window");
            checks.refresh(broken, true);
            waitUntil([&] { return finished == 2; }, "A forced refresh probes at once");
            check(launches(counter) == 3, "A forced refresh probes inside the retry window");
            qunsetenv("SKETCHYUP_FAKE_CODECS");
        }

        // presentation.describe in the desktop never blocks. With no cached check it reports the
        // check as pending, starts one, and returns; the answer arrives while events run. The CLI's
        // describe checks synchronously.
        {
            const auto oldPath = qgetenv("PATH");
            const auto bin = files.filePath("bin");
            check(QDir().mkpath(bin), "Blender directory fixture");
            Document described;
            described.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
            SceneSnapshot shot;
            shot.camera = SceneCamera{};
            const auto sceneA = createScene(described, "A", shot);
            shot.camera->yaw = 90;
            const auto sceneB = createScene(described, "B", shot);
            executePresentationCommand(
                described, QJsonObject{{"command", "camera_path.create"},
                                       {"name", "Orbit"},
                                       {"keyframes", QJsonArray{QString::number(sceneA),
                                                                QString::number(sceneB)}},
                                       {"timing", QJsonObject{{"framesPerSecond", 24},
                                                              {"transitionSteps", 1},
                                                              {"holdSteps", 1}}}});
            executePresentationCommand(described, QJsonObject{{"command", "render_preset.create"},
                                                              {"name", "Draft"},
                                                              {"width", 64},
                                                              {"height", 64},
                                                              {"samples", 1},
                                                              {"seed", 0},
                                                              {"engine", "cycles"}});
            Id pathId{}, presetId{};
            for (const auto &[id, path] : described.cameraPaths())
                if (path->name == "Orbit")
                    pathId = id;
            for (const auto &[id, preset] : described.renderPresets())
                if (preset->name == "Draft")
                    presetId = id;
            const auto encodersOf = [](const QJsonObject &description) {
                return description["blender"].toObject()["videoEncoders"].toObject();
            };
            qputenv("PATH", bin.toUtf8() + ":" + oldPath);
            {
                const auto counter = files.filePath("count-describe");
                launcher(files, "bin/blender", counter, 0);
                PresentationVideoChecks desktop;
                installPresentationVideoProbe(desktop);
                const auto pending = presentationDescription(described, pathId, {presetId}, true);
                const auto encoders = encodersOf(pending);
                check(encoders["checked"] == false && encoders["pending"] == true &&
                          encoders["message"] == "Checking the video encoder…",
                      "A describe with no cached check reports it as pending");
                check(!desktop.cached(BlenderJob::Options{}).has_value(),
                      "The describe returns before the probe can answer");
                check(pending["refusals"].toArray().contains(QJsonValue(
                          "Video cannot be confirmed yet: the Blender's video encoders are being "
                          "checked; ask again shortly")),
                      "A pending check is refused in words that say it is pending");
                waitUntil([&] { return desktop.cached(BlenderJob::Options{}).has_value(); },
                          "The background check fills the cache");
                const auto ready =
                    encodersOf(presentationDescription(described, pathId, {presetId}, true));
                check(ready["checked"] == true && ready["h264"] == true && ready["mpeg4"] == true,
                      "The next describe reports the cached check");
                presentationDescription(described, pathId, {presetId}, true);
                check(launches(counter) == 1, "Describe queries share one probe");
                setPresentationVideoProbe({});
            }
            {
                const auto counter = files.filePath("count-describe-cli");
                launcher(files, "bin/blender", counter, 0);
                PresentationVideoChecks blocking;
                installBlockingPresentationVideoProbe(blocking);
                const auto answer =
                    encodersOf(presentationDescription(described, pathId, {presetId}, true));
                check(answer["checked"] == true && answer["h264"] == true && launches(counter) == 1,
                      "The CLI's describe checks the Blender before it returns");
                setPresentationVideoProbe({});
            }
            qputenv("PATH", oldPath);
        }

        // The desktop batch never probes. start refuses a video until the cache holds a fresh
        // answer for this Blender, before any folder, job or frame. Once it does, the batch runs; a
        // changed Blender is refused again.
        {
            PresentationVideoChecks checks;
            const auto counter = files.filePath("count-desktop");
            const auto desktopBlender = blender(counter);
            PresentationJobStore jobs(files.filePath("desktop-store"), limits);
            const auto jobsBefore = jobs.jobs().size();
            auto videoPlan = plan(keyframes);
            videoPlan.video = true;
            const auto output = files.filePath("desktop-video");
            const std::string missing =
                "The video encoder has not been checked yet. Check again, then Start.";
            std::string message;
            PresentationExport refused(jobs);
            refused.setVideoChecks(&checks);
            try {
                refused.start(PresentationCapture::capture(doc, videoPlan), output, desktopBlender);
            } catch (const std::exception &error) {
                message = error.what();
            }
            check(message == missing && !QFileInfo::exists(output) &&
                      jobs.jobs().size() == jobsBefore && launches(counter) == 0,
                  "A missing check refuses a desktop video before any folder, job or launch");
            checks.refresh(desktopBlender);
            waitUntil([&] { return checks.cached(desktopBlender).has_value(); },
                      "The dialog's background check fills the cache");
            PresentationExport batch(jobs);
            batch.setVideoChecks(&checks);
            batch.start(PresentationCapture::capture(doc, videoPlan), output, desktopBlender);
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "A fresh cached check lets the desktop video batch complete");
            verifyVideos(output, 6);

            write(desktopBlender.executable, read(desktopBlender.executable) + "\n");
            const auto launchesBefore = launches(counter);
            message.clear();
            PresentationExport stale(jobs);
            stale.setVideoChecks(&checks);
            try {
                stale.start(PresentationCapture::capture(doc, videoPlan),
                            files.filePath("desktop-stale"), desktopBlender);
            } catch (const std::exception &error) {
                message = error.what();
            }
            check(message == missing && !QFileInfo::exists(files.filePath("desktop-stale")) &&
                      launches(counter) == launchesBefore,
                  "A changed Blender is refused until it is checked again, and nothing launches");
        }

        // The CLI keeps the synchronous check: a video batch without a cache checks the Blender
        // before start returns.
        {
            PresentationJobStore cliJobs(files.filePath("cli-store"), limits);
            const auto counter = files.filePath("count-cli");
            auto videoPlan = plan(keyframes);
            videoPlan.video = true;
            PresentationExport cli(cliJobs);
            cli.start(PresentationCapture::capture(doc, videoPlan), files.filePath("cli-video"),
                      blender(counter));
            check(launches(counter) == 1,
                  "Without a cache, start checks the Blender synchronously");
            wait(cli);
            check(cli.state() == PresentationJobState::Completed,
                  "The CLI's synchronously checked video batch completes");
        }

        // ADR 0173 §6: a batch's capture is its own. Deleting its camera path, or closing the
        // document, while it runs changes none of its frames, and a resume reads the captured
        // state, not the live document.
        {
            PresentationJobStore jobs(files.filePath("destroy-store"), limits);
            Document live;
            live.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
            const std::vector<std::pair<std::string, double>> views{
                {"A", -45}, {"B", 90}, {"C", 180}};
            std::vector<Id> scenes;
            for (const auto &[name, yaw] : views) {
                SceneSnapshot view;
                view.camera = SceneCamera{};
                view.camera->yaw = yaw;
                scenes.push_back(createScene(live, name, view));
            }
            const auto path = createCameraPath(live, "Orbit", scenes, {24, 1, 1});
            // The capture a resume uses is taken before the batch starts.
            const auto resumeCapture = PresentationCapture::capture(live, plan(scenes));
            const auto baselineOutput = files.filePath("destroy-baseline");
            PresentationExport baseline(jobs);
            baseline.start(PresentationCapture::capture(live, plan(scenes)), baselineOutput,
                           blender(files.filePath("count-destroy-baseline")));
            wait(baseline);
            check(baseline.state() == PresentationJobState::Completed &&
                      baseline.completedFrames() == 12,
                  "The baseline batch completes");

            // The camera path is deleted and the document closed after the first frame.
            PresentationExport batch(jobs);
            const auto output = files.filePath("destroy-live");
            batch.start(PresentationCapture::capture(live, plan(scenes)), output,
                        blender(files.filePath("count-destroy-live")));
            bool closed = false;
            QObject::connect(&batch, &PresentationExport::changed, &app, [&] {
                if (!closed && batch.completedFrames() == 1) {
                    closed = true;
                    eraseCameraPath(live, path);
                    live = Document{};
                }
            });
            wait(batch);
            check(closed && live.scenes().empty() && live.cameraPaths().empty(),
                  "The live document is closed while the batch runs");
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "The batch completes after its document is closed");
            verifyManifest(output, 12);
            verifySameFrames(output, baselineOutput, 6);
            const auto baselineManifest =
                QJsonDocument::fromJson(read(baselineOutput + "/manifest.json")).object();
            const auto liveManifest =
                QJsonDocument::fromJson(read(output + "/manifest.json")).object();
            check(liveManifest.value("captureSha256") == baselineManifest.value("captureSha256") &&
                      liveManifest.value("captureSha256") == resumeCapture.sha256(),
                  "The batch keeps the capture it was started with");

            // A canceled batch resumes with the live document still closed: the stored capture is
            // what it renders.
            PresentationExport interrupted(jobs);
            const auto resumedOutput = files.filePath("destroy-resumed");
            const auto interruptedId = interrupted.start(
                resumeCapture, resumedOutput, blender(files.filePath("count-destroy-interrupted")));
            bool requested = false;
            QObject::connect(&interrupted, &PresentationExport::changed, &app, [&] {
                if (!requested && interrupted.completedFrames() == 2) {
                    requested = true;
                    interrupted.cancel();
                }
            });
            wait(interrupted);
            check(interrupted.state() == PresentationJobState::Canceled && live.scenes().empty(),
                  "The batch cancels with the document still closed");
            PresentationExport resume(jobs);
            resume.retry(interruptedId, blender(files.filePath("count-destroy-resumed")));
            wait(resume);
            check(resume.state() == PresentationJobState::Completed,
                  "A resume completes from the captured state");
            verifyManifest(resumedOutput, 12);
            verifySameFrames(resumedOutput, baselineOutput, 6);
            check(QJsonDocument::fromJson(read(resumedOutput + "/manifest.json"))
                          .object()
                          .value("captureSha256") == resumeCapture.sha256(),
                  "The resumed batch keeps the captured state's digest");
        }

        // ADR 0173 §6: a single render that is queued or running is not the batch's. Cancel, stop
        // and resume leave its state, its worker process and its store record as they were.
        {
            QDir().mkpath(files.filePath("render"));
            // One slot: the second render waits behind the first, so it stays queued.
            RenderQueue queue(files.filePath("render-queue"), RenderJobStore::Limits{}, 1);
            RenderOptions renderOptions;
            renderOptions.settings = {64, 64, 1, 0};
            const auto prepared =
                PreparedRender::prepare(RenderSnapshot::capture(doc, renderOptions));
            BlenderJob::Options renderWorker = options;
            renderWorker.timeoutMs = 900000; // It hangs until it is canceled.
            renderWorker.executable =
                blender_stand_in::place(files.filePath("render/blender-hang"), "hang");
            const auto running = queue.enqueue(prepared, renderWorker);
            const auto queued = queue.enqueue(prepared, renderWorker);
            waitUntil(
                [&] {
                    return renderRecord(queue, running).state == RenderJobState::Running &&
                           queue.workerProcessId(running) > 0;
                },
                "The unrelated single render runs");
            check(renderRecord(queue, queued).state == RenderJobState::Queued,
                  "The second single render waits in the queue");
            const auto runningDirectory = files.filePath("render-queue") + "/" + running;
            check(QFileInfo(runningDirectory).isDir(), "The running render has its job folder");
            const auto runningBefore = renderRecord(queue, running);
            const auto queuedBefore = renderRecord(queue, queued);
            const auto pid = queue.workerProcessId(running);
            const auto unrelatedUnchanged = [&](const char *when) {
                const auto now = renderRecord(queue, running);
                const auto waiting = renderRecord(queue, queued);
                const auto same = [&](bool unchanged, const char *field) {
                    if (!unchanged)
                        throw std::runtime_error(std::string(when) + ": the unrelated render's " +
                                                 field + " changed");
                };
                if (now.state != RenderJobState::Running)
                    throw std::runtime_error(std::string(when) + ": the unrelated render is " +
                                             std::to_string(int(now.state)) + ", " +
                                             now.message.toStdString());
                same(now.attempts == runningBefore.attempts, "attempts");
                same(now.manifestHash == runningBefore.manifestHash, "manifest");
                same(queue.workerProcessId(running) == pid, "worker process");
                same(now.updatedMs == runningBefore.updatedMs, "store record");
                // It has no output yet: no result is written beside its record.
                // (RenderQueue::result is not called here: for a job that has no result it marks
                // the job Failed in jobs().)
                same(!QFileInfo::exists(runningDirectory + "/result.json"), "output");
                same(waiting.state == RenderJobState::Queued, "queued state");
                same(waiting.updatedMs == queuedBefore.updatedMs, "queued store record");
            };

            PresentationJobStore jobs(files.filePath("unrelated-store"), limits);
            PresentationExport canceledBatch(jobs);
            const auto canceledId =
                canceledBatch.start(PresentationCapture::capture(doc, plan(keyframes)),
                                    files.filePath("unrelated-canceled"),
                                    blender(files.filePath("count-unrelated-canceled")));
            bool requested = false;
            QObject::connect(&canceledBatch, &PresentationExport::changed, &app, [&] {
                if (!requested && canceledBatch.completedFrames() == 2) {
                    requested = true;
                    canceledBatch.cancel();
                }
            });
            wait(canceledBatch);
            check(canceledBatch.state() == PresentationJobState::Canceled,
                  "The batch cancels beside an unrelated render");
            unrelatedUnchanged("Cancel leaves the unrelated single render as it was");
            PresentationExport resumedBatch(jobs);
            resumedBatch.retry(canceledId, blender(files.filePath("count-unrelated-resumed")));
            wait(resumedBatch);
            check(resumedBatch.state() == PresentationJobState::Completed,
                  "The canceled batch resumes beside an unrelated render");
            unrelatedUnchanged("Resume leaves the unrelated single render as it was");

            // The quit prompt stops a running batch; PresentationBatches::stop reaches this call.
            PresentationExport stoppedBatch(jobs);
            const auto stoppedId =
                stoppedBatch.start(PresentationCapture::capture(doc, plan(keyframes)),
                                   files.filePath("unrelated-stopped"),
                                   blender(files.filePath("count-unrelated-stopped")));
            bool halted = false;
            QObject::connect(&stoppedBatch, &PresentationExport::changed, &app, [&] {
                if (!halted && stoppedBatch.completedFrames() == 1) {
                    halted = true;
                    stoppedBatch.stop();
                }
            });
            wait(stoppedBatch);
            check(stoppedBatch.state() != PresentationJobState::Completed,
                  "A stopped batch does not complete");
            unrelatedUnchanged("The stop leaves the unrelated single render as it was");
            PresentationExport stoppedResume(jobs);
            stoppedResume.retry(stoppedId,
                                blender(files.filePath("count-unrelated-stopped-resumed")));
            wait(stoppedResume);
            check(stoppedResume.state() == PresentationJobState::Completed,
                  "The stopped batch resumes beside an unrelated render");
            unrelatedUnchanged(
                "Resuming the stopped batch leaves the unrelated single render as it was");

            queue.cancel(queued);
            queue.cancel(running);
            waitUntil(
                [&] {
                    return renderRecord(queue, running).state != RenderJobState::Running &&
                           renderRecord(queue, running).state != RenderJobState::Canceling;
                },
                "The unrelated single render is stopped at the end");
        }

        // Real Blender, when the explicit opt-in is set: one batch from one capture.
        if (real) {
            PresentationJobStore jobs(files.filePath("real-store"), limits);
            PresentationExport batch(jobs);
            BlenderJob::Options real = options;
            real.executable = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
            real.timeoutMs = 180000;
            const auto before = encodeDocument(doc);
            // The plan is deterministic, so the planned frames of a second capture are the batch's.
            const auto plannedFrames = PresentationCapture::capture(doc, plan(keyframes)).frames();
            batch.start(PresentationCapture::capture(doc, plan(keyframes)), files.filePath("real"),
                        real);
            wait(batch);
            check(batch.state() == PresentationJobState::Completed && batch.completedFrames() == 12,
                  "Real Blender completes every planned frame");
            verifyManifest(files.filePath("real"), 12);
            verifyPoses(files.filePath("real"), plannedFrames);
            reportDeviation("real Blender",
                            verifyRenderedPoses(files.filePath("real"), plannedFrames));
            check(encodeDocument(doc) == before, "Real batch leaves the live document unchanged");
            // Real video: an H.264 MP4 per preset that ffprobe reads back; a cancel writes none.
            auto videoPlan = plan(keyframes);
            videoPlan.video = true;
            PresentationJobStore videoJobs(files.filePath("real-video-store"), limits);
            PresentationExport video(videoJobs);
            bool stopped = false;
            QObject::connect(&video, &PresentationExport::changed, &app, [&] {
                if (!stopped && video.completedFrames() >= 1) {
                    stopped = true;
                    video.cancel();
                }
            });
            const auto videoId = video.start(PresentationCapture::capture(doc, videoPlan),
                                             files.filePath("real-video"), real);
            wait(video);
            check(video.state() == PresentationJobState::Canceled &&
                      videoBytes(files.filePath("real-video"), {"01-Draft", "02-Check"}) == 0,
                  "A real canceled batch writes no video");
            PresentationExport videoResume(videoJobs);
            videoResume.retry(videoId, real);
            wait(videoResume);
            check(videoResume.state() == PresentationJobState::Completed,
                  "Real retry completes the canceled video batch");
            verifyVideos(files.filePath("real-video"), 6);
            for (const auto *preset : {"01-Draft", "02-Check"})
                check(probeVideo(files.filePath("real-video") + "/" + preset + "/video.mp4") ==
                          "h264,6",
                      "ffprobe reports H.264 with every planned frame");

            // The explicit MPEG-4 Part 2 fallback (UX_DESIGN 17.12 choice 1) is read back as mpeg4.
            auto mpeg4Plan = plan(keyframes);
            mpeg4Plan.video = true;
            mpeg4Plan.videoCodec = PresentationVideoCodec::MPEG4;
            PresentationJobStore mpeg4Jobs(files.filePath("real-mpeg4-store"), limits);
            PresentationExport mpeg4Batch(mpeg4Jobs);
            mpeg4Batch.start(PresentationCapture::capture(doc, mpeg4Plan),
                             files.filePath("real-mpeg4"), real);
            wait(mpeg4Batch);
            check(mpeg4Batch.state() == PresentationJobState::Completed &&
                      mpeg4Batch.completedFrames() == 12,
                  "Real Blender completes every frame of an MPEG-4 batch");
            verifyVideos(files.filePath("real-mpeg4"), 6, "mpeg4");
            for (const auto *preset : {"01-Draft", "02-Check"})
                check(probeVideo(files.filePath("real-mpeg4") + "/" + preset + "/video.mp4") ==
                          "mpeg4,6",
                      "ffprobe reports MPEG-4 with every planned frame");

            // The worker's own hash check refuses a frame that does not match its digest.
            BlenderVideoRequest wrong;
            wrong.width = 64;
            wrong.height = 64;
            wrong.framesPerSecond = 24;
            const auto frame = files.filePath("real-video") + "/01-Draft/frames/frame-0001.png";
            wrong.frames.push_back({frame, "0000", QFileInfo(frame).size()});
            BlenderJob encoder;
            encoder.encode(wrong, real);
            waitUntil([&] { return encoder.done(); }, "Encode refusal finishes");
            check(encoder.phase() == BlenderJob::Phase::Failed &&
                      encoder.report().value("code") == "invalid_frame",
                  "The real worker refuses a frame whose hash does not match");

            // ADR 0173 §6: identical renders give identical PNG bytes. Two runs of the same
            // two-frame batch give the same SHA-256 for every frame, so Blender's stamp and its
            // Cycles statistics do not reach the file.
            const auto frameDigests = [&](const QString &name) {
                PresentationJobStore twice(files.filePath(name + "-store"), limits);
                PresentationExport run(twice);
                run.start(PresentationCapture::capture(doc, plan({first, second}, {24, 1, 0})),
                          files.filePath(name), real);
                wait(run);
                check(run.state() == PresentationJobState::Completed && run.completedFrames() == 4,
                      "Real Blender completes both runs of the two-frame batch");
                verifyManifest(files.filePath(name), 4);
                QStringList digests;
                const auto manifest =
                    QJsonDocument::fromJson(read(files.filePath(name) + "/manifest.json")).object();
                for (const auto &presetValue : manifest.value("presets").toArray())
                    for (const auto &entryValue : presetValue.toObject().value("frames").toArray())
                        digests << entryValue.toObject().value("sha256").toString();
                return digests;
            };
            const auto firstDigests = frameDigests("png-first");
            const auto secondDigests = frameDigests("png-second");
            check(firstDigests.size() == 4 && firstDigests == secondDigests,
                  "Two runs of the same two-frame batch give identical PNG bytes");
            for (const auto &digest : firstDigests)
                std::cout << "PNG sha256 " << digest.toStdString() << '\n';

            std::cout << "Real Blender presentation batch and video verified\n";
            return 0;
        }
        // R107.c1 (ADR 0181 §3.3, owner decision 8): a batch whose frames show DEM terrain lists the notice in
        // its capture and its manifest, once each. The batch above, with no terrain, has no credits key.
        {
            const std::string fixtures = std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/";
            Document terrain;
            importDemFixture(terrain, fixtures, "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
            importDemFixture(terrain, fixtures, "plane-f32-strip-none-le.tif", DemProduct::Dep3, "NAVD88");
            SceneSnapshot terrainView;
            terrainView.camera = SceneCamera{};
            const auto terrainFirst = createScene(terrain, "A", terrainView);
            terrainView.camera->yaw = 90;
            const auto terrainSecond = createScene(terrain, "B", terrainView);
            const auto capture =
                PresentationCapture::capture(terrain, plan({terrainFirst, terrainSecond}));
            check(capture.credits() == std::vector<std::string>{demCreditUsgs},
                  "The capture carries the 3DEP notice once");
            PresentationJobStore terrainJobs(files.filePath("terrain-store"), limits);
            PresentationExport terrainBatch(terrainJobs);
            const auto terrainOutput = files.filePath("terrain");
            terrainBatch.start(capture, terrainOutput, blender(files.filePath("count-terrain")));
            wait(terrainBatch);
            check(terrainBatch.state() == PresentationJobState::Completed,
                  "The terrain batch completes");
            const auto manifest =
                QJsonDocument::fromJson(read(terrainOutput + "/manifest.json")).object();
            check(manifest.value("credits") == QJsonArray{QString::fromUtf8(demCreditUsgs)},
                  "The batch manifest lists the notice");
        }
        std::cout << "Presentation batch capture, frame sequence, cancellation, resume, damage and "
                     "refusal checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
